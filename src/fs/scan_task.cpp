// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "scan_task.h"
#include "utils.h"
#include "model/messages.h"
#include "fs/messages.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs;

scan_task_t::scan_task_t(model::cluster_ptr_t cluster_, std::string_view folder_id_,
                         const config::fs_config_t &config_) noexcept
    : folder_id{folder_id_}, cluster{cluster_}, config{config_}, current_diff{nullptr} {
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

    bytes_left = config.bytes_scan_iteration_limit;
    files_left = config.files_scan_iteration_limit;
    assert(bytes_left > 0);
    assert(files_left > 0);

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
        seen_paths.insert({std::string(file->get_name()), file->get_path()});
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
    if (ec) {
        return scan_errors_t{scan_error_t{dir, ec}};
    }
    if (!exists) {
        return true;
    }

    auto record_path = [&](const bfs::path &file) {
        auto relative = relativize(file, root);
        auto str = boost::nowide::narrow(relative.generic_wstring());
        seen_paths.insert({std::move(str), file});
    };

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
                seen_paths.insert({std::string(file.get_name()), file.get_path()});
                removed.put(it.item);
            }
        }
        seen_paths.insert({str, dir});
        errors.push_back(scan_error_t{dir, ec});
    } else {
        for (; it != bfs::directory_iterator(); ++it) {
            auto &child = *it;
            record_path(child);
            sys::error_code ec;
            auto status = bfs::symlink_status(child, ec);
            if (ec && (status.type() != bfs::file_type::symlink)) {
                errors.push_back(scan_error_t{child, ec});
                continue;
            }
            auto rp = relativize(child, root).generic_string();
            auto file = files.by_name(rp);
            if (file) {
                files_queue.push_back(file);
                removed.put(file);
                if (status.type() == bfs::file_type::directory) {
                    dirs_queue.push_back(child);
                }
                continue;
            }

            auto file_type = status.type();
            proto::FileInfo metadata;
            proto::set_name(metadata, rp);

            if (file_type == bfs::file_type::regular || file_type == bfs::file_type::directory) {
                auto modification_time = bfs::last_write_time(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_modified_s(metadata, to_unix(modification_time));

                auto permissions = static_cast<uint32_t>(status.permissions());
                proto::set_permissions(metadata, permissions);
            }
            if (file_type == bfs::file_type::regular) {
                proto::set_type(metadata, proto::FileInfoType::FILE);
                auto sz = bfs::file_size(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_size(metadata, sz);
            } else if (file_type == bfs::file_type::directory) {
                proto::set_type(metadata, proto::FileInfoType::DIRECTORY);
                dirs_queue.push_back(child);
            } else if (file_type == bfs::file_type::symlink) {
                auto target = bfs::read_symlink(child, ec);
                if (ec) {
                    errors.push_back(scan_error_t{dir, ec});
                    continue;
                }
                proto::set_symlink_target(metadata, target.string());
                proto::set_type(metadata, proto::FileInfoType::SYMLINK);
            } else {
                LOG_WARN(log, "unknown/unimplemented file type {} : {}", (int)status.type(), bfs::path(child).string());
                continue;
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

scan_result_t scan_task_t::advance_file(file_info_t &file) noexcept {
    if (file->is_file()) {
        return advance_regular_file(file);
    } else if (file->is_dir()) {
        return unchanged_meta_t{file};
    } else {
        assert(file->is_link());
        return advance_symlink_file(file);
    }
}

scan_result_t scan_task_t::advance_regular_file(file_info_t &file) noexcept {
    sys::error_code ec;

    auto path = file->get_path();
    auto meta = proto::FileInfo();
    bool changed = false;

    auto sz = bfs::file_size(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }

    proto::set_size(meta, sz);
    if (sz != (size_t)file->get_size()) {
        changed = true;
    }

    auto modified = bfs::last_write_time(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }
    auto modified_s = to_unix(modified);
    proto::set_modified_s(meta, modified_s);
    if (modified_s != file->get_modified_s()) {
        changed = true;
    }

    auto status = bfs::status(path, ec);
    if (ec) {
        return file_error_t{path, ec};
    }
    auto permissions = static_cast<uint32_t>(status.permissions());
    proto::set_permissions(meta, permissions);
    if (permissions != file->get_permissions()) {
        changed = true;
    }

    if (changed) {
        using FT = proto::FileInfoType;
        proto::set_name(meta, file->get_name());
        proto::set_type(meta, FT::FILE);
        return changed_meta_t{file, std::move(meta)};
    }

    return unchanged_meta_t{file};
}

scan_result_t scan_task_t::advance_symlink_file(file_info_t &file) noexcept {
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

    auto target_str = target.string();
    if (target_str == file->get_link_target()) {
        return unchanged_meta_t{file};
    } else {
        using FT = proto::FileInfoType;
        auto meta = proto::FileInfo();
        proto::set_name(meta, file->get_name());
        proto::set_type(meta, FT::SYMLINK);
        proto::set_symlink_target(meta, std::move(target_str));
        return changed_meta_t{file, std::move(meta)};
    }
}

scan_result_t scan_task_t::advance_unknown_file(unknown_file_t &file) noexcept {
    auto &path = file.path;
    if (!is_temporal(file.path.filename())) {
        return unknown_file_t{std::move(path), std::move(file.metadata)};
    }

    auto peer_file = model::file_info_ptr_t{};
    auto peer_counter = proto::Counter();
    auto relative_path = [&]() -> std::string {
        auto rp = relativize(path, root);
        auto name = path.filename();
        auto name_str = name.string();
        auto new_name = name_str.substr(0, name_str.size() - tmp_suffix.size());
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
            seen_paths.insert({std::string(f->get_name()), path});
            if (!peer_file) {
                peer_file = std::move(f);
                peer_counter = peer_file->get_version()->get_best();
            } else {
                auto &c = f->get_version()->get_best();
                if (proto::get_value(peer_counter) < proto::get_value(c)) {
                    peer_counter = c;
                    peer_file = std::move(f);
                    break;
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

    auto modified_time = bfs::last_write_time(path, ec);
    if (ec) {
        LOG_DEBUG(log, "removing outdated temporally {}, cannot get last modification: {}", path.string(),
                  ec.message());
        bfs::remove(path, ec);
        return file_error_t{path, ec};
    }
    auto modified_at = to_unix(modified_time);
    auto now = std::time(nullptr);
    if (modified_at + config.temporally_timeout <= now) {
        LOG_DEBUG(log, "removing outdated temporally {}", path.string());
        bfs::remove(path, ec);
        if (ec) {
            return file_error_t{path, ec};
        }
        return incomplete_removed_t{peer_file};
    }

    auto actual_size = proto::get_size(file.metadata);
    bool size_matches = static_cast<std::int64_t>(peer_file->get_size()) == actual_size;

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

void scan_task_t::push(model::diff::cluster_diff_t *update, std::int64_t bytes_consumed) noexcept {
    if (current_diff) {
        current_diff = current_diff->assign_sibling(update);
    } else {
        update_diff.reset(update);
        current_diff = update;
    }
    bytes_left -= bytes_consumed;
    --files_left;
}

auto scan_task_t::get_seen_paths() const noexcept -> const seen_paths_t & { return seen_paths; }

auto scan_task_t::guard(r::actor_base_t &actor, r::address_ptr_t coordinator) noexcept -> send_guard_t {
    return send_guard_t(*this, actor, coordinator);
}

scan_task_t::send_guard_t::send_guard_t(scan_task_t &task_, r::actor_base_t &actor_,
                                        r::address_ptr_t coordinator_) noexcept
    : task{task_}, actor{actor_}, coordinator{coordinator_}, force_send{false}, manage_progress{false} {}

void scan_task_t::send_guard_t::send_by_force() noexcept { force_send = true; }

void scan_task_t::send_guard_t::send_progress() noexcept { manage_progress = true; }

scan_task_t::send_guard_t::~send_guard_t() {
    auto consume = force_send || task.bytes_left <= 0 || task.files_left <= 0;
    if (consume) {
        auto diff = std::move(task.update_diff);
        if (diff) {
            task.current_diff = nullptr;
            task.bytes_left = task.config.bytes_scan_iteration_limit;
            task.files_left = task.config.files_scan_iteration_limit;
            actor.send<model::payload::model_update_t>(coordinator, std::move(diff), nullptr);
            if (manage_progress) {
                auto &sup = actor.get_supervisor();
                auto address = actor.get_address();
                auto message = rotor::make_routed_message<payload::scan_progress_t>(coordinator, address, &task);
                sup.put(message);
            }
        }
    } else if (manage_progress) {
        actor.send<payload::scan_progress_t>(actor.get_address(), &task);
    }
}

//
