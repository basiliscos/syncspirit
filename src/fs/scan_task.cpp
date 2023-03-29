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
    files = &my_folder->get_file_infos();

    log = utils::get_logger("fs.scan");
}

scan_task_t::~scan_task_t() {}

std::string_view scan_task_t::get_folder_id() const noexcept { return folder_id; }

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

    return !dirs_queue.empty();
}

scan_result_t scan_task_t::advance_dir(const bfs::path &dir) noexcept {
    sys::error_code ec;

    bool exists = bfs::exists(dir, ec);
    if (ec || !exists) {
        return scan_errors_t{scan_error_t{dir, ec}};
    }

    auto push = [this](const bfs::path &path, file_type_t file_type) noexcept {
        auto rp = relativize(path, root);
        auto file = files->by_name(rp.path.string());
        if (file) {
            files_queue.push_back(file_info_t{file, rp.temp});
        } else {
            unknown_files_queue.push_back(unknown_file_t{rp.path, file_type});
        }
    };

    scan_errors_t errors;
    auto it = bfs::directory_iterator(dir, ec);
    if (ec) {
        errors.push_back(scan_error_t{dir, ec});
    } else {
        for (; it != bfs::directory_iterator(); ++it) {
            sys::error_code ec;
            auto &child = *it;
            bool is_dir = bfs::is_directory(child, ec);

            if (is_dir) {
                dirs_queue.push_back(child);
                continue;
            }

            bool is_reg = bfs::is_regular_file(child, ec);
            if (is_reg) {
                push(child.path(), file_type_t::regular);
                continue;
            }

            bool is_symlink = bfs::is_symlink(child, ec);
            if (is_symlink) {
                push(child.path(), file_type_t::symlink);
                continue;
            }
        }
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

    return incomplete_t{info.file};
}
