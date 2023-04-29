// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "scan_task.h"
#include "utils.h"

using namespace syncspirit::fs;

scan_task_t::scan_task_t(model::cluster_ptr_t cluster_, std::string_view folder_id_,
                         const config::fs_config_t &config_) noexcept
    : folder_id{folder_id_}, cluster{cluster_}, config{config_} {
    auto &fm = cluster->get_folders();
    auto folder = fm.by_id(folder_id);
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

    log = utils::get_logger("fs.scan");
}

scan_task_t::~scan_task_t() {}

const std::string &scan_task_t::get_folder_id() const noexcept { return folder_id; }

scan_result_t scan_task_t::advance() noexcept {
    if (!unknown_files_queue.empty()) {
        auto path = unknown_files_queue.front();
        unknown_files_queue.pop_front();
        return path;
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
            if (ec) {
                errors.push_back(scan_error_t{child, ec});
                continue;
            }
            if (status.type() == bfs::file_type::directory_file) {
                dirs_queue.push_back(child);
                continue;
            }
            auto rp = relativize(child, root);
            auto file = files.by_name(rp.path.string());
            if (file) {
                files_queue.push_back(file_info_t{file, rp.temp});
                removed.put(file);
                continue;
            }

            proto::FileInfo metadata;
            metadata.set_name(rp.path.string());
            if (status.type() == bfs::file_type::regular_file) {
                metadata.set_type(proto::FileInfoType::FILE);
                auto sz = bfs::file_size(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                metadata.set_size(sz);
            } else if (status.type() == bfs::file_type::symlink_file) {
                metadata.set_type(proto::FileInfoType::SYMLINK);
            } else {
                LOG_WARN(log, "unknown/unimplemented file type {} : {}", (int)status.type(), bfs::path(child).string());
            }
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

scan_result_t scan_task_t::advance_file(const file_info_t &info) noexcept {
    sys::error_code ec;
    auto file = info.file.get();
    auto path = info.file->get_path();
    if (info.temp) {
        path = make_temporal(path);
    }

    auto sz = bfs::file_size(path, ec);
    if (ec) {
        return file_error_t{file, ec};
    }

    if (!info.temp) {
        if (sz != (size_t)file->get_size()) {
            return changed_meta_t{info.file};
        }

        auto modified = bfs::last_write_time(path, ec);
        if (ec) {
            return file_error_t{file, ec};
        }
        if (modified != file->get_modified_s()) {
            return changed_meta_t{info.file};
        }
        return unchanged_meta_t{info.file};
    }

    auto modified_at = bfs::last_write_time(path, ec);
    if (ec) {
        return file_error_t{file, ec};
    }

    auto now = std::time(nullptr);
    if (modified_at + config.temporally_timeout <= now) {
        LOG_DEBUG(log, "removing outdated temporally {}", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{file, ec};
        }
        return incomplete_removed_t{file};
    }

    auto source = file->get_source();
    if (!source) {
        LOG_DEBUG(log, "source file missing for {}, removing", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{file, ec};
        }
        return incomplete_removed_t{file};
    }

    if (sz != (size_t)source->get_size()) {
        LOG_DEBUG(log, "removing size-mismatched temporally {}", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{file, ec};
        }
        return incomplete_removed_t{file};
    }

    auto opt = file_t::open_read(path);
    if (!opt) {
        LOG_DEBUG(log, "try to remove temporally {}, which cannot open ", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{file, ec};
        }
        return incomplete_removed_t{file};
    }

    auto &opened_file = opt.assume_value();
    return incomplete_t{info.file, file_ptr_t(new file_t(std::move(opened_file)))};
}
