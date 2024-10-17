// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "scan_task.h"
#include "utils.h"
#include "model/misc/version_utils.h"

using namespace syncspirit::fs;

scan_task_t::scan_task_t(model::cluster_ptr_t cluster_, std::string_view folder_id_,
                         const config::fs_config_t &config_) noexcept
    : folder_id{folder_id_}, cluster{cluster_}, config{config_} {
    auto &fm = cluster->get_folders();
    folder = fm.by_id(folder_id);
    if (!folder) {
        return;
    }

    auto my_folder = folder->get_folder_infos().by_device(*cluster->get_device());
    if (!my_folder) {
        return;
    }

    auto path = folder->get_path();
    dirs_queue.push_back(path);

    root = path;

    auto &orig_files = my_folder->get_file_infos();
    for (auto &it : orig_files) {
        files.put(it.item);
    }

    log = utils::get_logger("fs.scan_task");
}

scan_task_t::~scan_task_t() {}

const std::string &scan_task_t::get_folder_id() const noexcept { return folder_id; }

scan_result_t scan_task_t::advance() noexcept {
    if (!unknown_files_queue.empty()) {
        auto &file = unknown_files_queue.front();
        auto &&r = advance_unknown_file(file);
        unknown_files_queue.pop_front();
        return r;
    }
    if (!files_queue.empty()) {
        auto &file = files_queue.front();
        auto &&r = advance_file(file);
        files_queue.pop_front();
        return r;
    } else if (!dirs_queue.empty()) {
        auto &path = dirs_queue.front();
        auto &&r = advance_dir(path);
        dirs_queue.pop_front();
        return r;
    }
    if (!dirs_queue.empty()) {
        return true;
    }
    if (files.size() != 0) {
        auto file = files.begin()->item;
        files.remove(file);
        if (file->is_deleted()) {
            return unchanged_meta_t{std::move(file)};
        } else {
            return removed_t{std::move(file)};
        }
    }

    return false;
}

scan_result_t scan_task_t::advance_dir(const bfs::path &dir) noexcept {
    sys::error_code ec;

    bool exists = bfs::exists(dir, ec);
    if (ec || !exists) {
        if (ec == sys::errc::no_such_file_or_directory) {
            return true;
        } else {
            return scan_errors_t{scan_error_t{dir, ec}};
        }
    }

    scan_errors_t errors;
    auto it = bfs::directory_iterator(dir, ec);
    auto removed = model::file_infos_map_t{};
    if (ec) {
        auto str = std::string{};
        if (dir != root) {
            str = bfs::relative(dir, root).string() + "/";
        }
        for (auto &it : files) {
            auto &file = *it.item;
            bool remove = str.empty() || (file.get_name().find(str) == 0);
            if (remove) {
                removed.put(it.item);
            }
        }
        errors.push_back(scan_error_t{dir, ec});
    } else {
        for (; it != bfs::directory_iterator(); ++it) {
            auto &child = *it;
            sys::error_code ec;
            auto status = bfs::symlink_status(child, ec);
            if (ec && (status.type() != bfs::file_type::symlink_file)) {
                errors.push_back(scan_error_t{child, ec});
                continue;
            }
            auto rp = relativize(child, root).generic_string();
            auto file = files.by_name(rp);
            if (file) {
                files_queue.push_back(file);
                removed.put(file);
                if (status.type() == bfs::file_type::directory_file) {
                    dirs_queue.push_back(child);
                }
                continue;
            }

            proto::FileInfo metadata;
            metadata.set_name(rp);
            if (status.type() == bfs::file_type::regular_file) {
                metadata.set_type(proto::FileInfoType::FILE);
                auto sz = bfs::file_size(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                metadata.set_size(sz);

                auto modification_time = bfs::last_write_time(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                metadata.set_modified_s(modification_time);

            } else if (status.type() == bfs::file_type::directory_file) {
                metadata.set_type(proto::FileInfoType::DIRECTORY);
                dirs_queue.push_back(child);
            } else if (status.type() == bfs::file_type::symlink_file) {
                auto target = bfs::read_symlink(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                metadata.set_symlink_target(target.string());
                metadata.set_type(proto::FileInfoType::SYMLINK);
            } else {
                LOG_WARN(log, "unknown/unimplemented file type {} : {}", (int)status.type(), bfs::path(child).string());
                continue;
            }

            metadata.set_permissions(static_cast<uint32_t>(status.permissions()));
            unknown_files_queue.push_back(unknown_file_t{child, std::move(metadata)});
        }
    }
    for (auto &it : removed) {
        files.remove(it.item);
    }
    if (!errors.empty()) {
        return errors;
    }

    return true;
}

scan_result_t scan_task_t::advance_file(const file_info_t &file) noexcept {
    if (file->is_file()) {
        return advance_regular_file(file);
    } else if (file->is_dir()) {
        return unchanged_meta_t{file};
    } else {
        assert(file->is_link());
        return advance_symlink_file(file);
    }
}

scan_result_t scan_task_t::advance_regular_file(const file_info_t &file) noexcept {
    sys::error_code ec;

    auto path = file->get_path();
    auto meta = proto::FileInfo();
    bool changed = false;

    auto sz = bfs::file_size(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    meta.set_size(sz);
    if (sz != (size_t)file->get_size()) {
        changed = true;
    }

    auto modified = bfs::last_write_time(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    meta.set_modified_s(modified);
    if (modified != file->get_modified_s()) {
        changed = true;
    }

    if (changed) {
        using FT = proto::FileInfoType;
        meta.set_name(std::string(file->get_name()));
        meta.set_type(FT::FILE);
        return changed_meta_t{file, std::move(meta)};
    }

    return unchanged_meta_t{file};
}

scan_result_t scan_task_t::advance_symlink_file(const file_info_t &file) noexcept {
    auto path = file->get_path();

    if (!bfs::is_symlink(path)) {
        LOG_CRITICAL(log, "not implemented change tracking: symlink -> non-symblink");
        return unchanged_meta_t{file};
    }

    sys::error_code ec;
    auto target = bfs::read_symlink(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    if (target.string() == file->get_link_target()) {
        return unchanged_meta_t{file};
    } else {
        using FT = proto::FileInfoType;
        auto meta = proto::FileInfo();
        meta.set_name(std::string(file->get_name()));
        meta.set_type(FT::SYMLINK);
        meta.set_symlink_target(target.string());
        return changed_meta_t{file, std::move(meta)};
    }
}

scan_result_t scan_task_t::advance_unknown_file(const unknown_file_t &file) noexcept {
    if (!is_temporal(file.path.filename())) {
        return file;
    }

    auto &path = file.path;
    auto peer_file = model::file_info_ptr_t{};
    auto relative_path = [&]() -> std::string {
        auto rp = relativize(path, root);
        auto name = path.filename();
        auto name_str = name.string();
        auto new_name = name_str.substr(0, name.size() - tmp_suffix.size());
        auto new_path = rp.parent_path() / new_name;
        return new_path.generic_string();
    }();
    for (auto &it : folder->get_folder_infos()) {
        auto &folder_info = it.item;
        if (folder_info->get_device() == cluster->get_device()) {
            continue;
        }
        auto &files = folder_info->get_file_infos();
        auto f = files.by_name(relative_path);
        if (f) {
            if (!peer_file) {
                peer_file = std::move(f);
            } else {
                using V = model::version_relation_t;
                auto r = model::compare(peer_file->get_version(), f->get_version());
                if (r == V::older) {
                    peer_file = std::move(f);
                }
            }
        }
    }

    sys::error_code ec;
    if (!peer_file) {
        LOG_INFO(log, "source cannot be found for temporal file {}, removing orphan", relative_path);
        bfs::remove(path, ec);
        return orphaned_removed_t{path};
    }

    auto modified_at = bfs::last_write_time(path, ec);
    if (ec) {
        LOG_DEBUG(log, "removing outdated temporally {}, cannot get last modification: {}", path.string(),
                  ec.message());
        bfs::remove(path, ec);
        return file_error_t{path, ec};
    }

    auto now = std::time(nullptr);
    if (modified_at + config.temporally_timeout <= now) {
        LOG_DEBUG(log, "removing outdated temporally {}", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    bool size_matches = static_cast<size_t>(peer_file->get_size()) == file.metadata.size();

    if (!size_matches) {
        LOG_DEBUG(log, "removing temporally '{}' because of size-mismatch or outdated source", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        LOG_DEBUG(log, "try to remove temporally {}, which cannot open ", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto &opened_file = opt.assume_value();
    return incomplete_t{peer_file, file_ptr_t(new file_t(std::move(opened_file)))};
}
