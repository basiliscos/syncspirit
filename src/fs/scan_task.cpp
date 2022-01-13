#include "scan_task.h"
#include "utils.h"

using namespace syncspirit::fs;

scan_task_t::scan_task_t(model::cluster_ptr_t cluster_, std::string_view folder_id, const config::fs_config_t& config_) noexcept:
    cluster{cluster_}, config{config_} {
    auto& fm = cluster->get_folders();
    auto folder = fm.by_id(folder_id);
    if (!folder) {
        return;
    }

    auto my_folder = folder->get_folder_infos().by_device(cluster->get_device());
    if (!my_folder) {
        return;
    }

    auto path = folder->get_path();
    dirs_queue.push_back(path);

    root = path;
    files = &my_folder->get_file_infos();

    log = utils::get_logger("fs.scan");
}

scan_task_t::~scan_task_t() {

}


scan_result_t scan_task_t::advance() noexcept {
    if (!files_queue.empty()) {
        auto& file = files_queue.front();
        auto&& r = advance_file(file);
        files_queue.pop_front();
        return r;
    }
    else if (!dirs_queue.empty()) {
        auto& path = dirs_queue.front();
        auto&& r = advance_dir(path);
        dirs_queue.pop_front();
        return r;
    }

    return !dirs_queue.empty();
}

scan_result_t scan_task_t::advance_dir(const bfs::path& dir) noexcept {
    sys::error_code ec;

    bool exists = bfs::exists(dir, ec);
    if (ec || !exists) {
        return errors_t{ scan_error_t{dir, scan_context_t::enter_dir,  ec}};
    }

    auto push = [this](const bfs::path& path) noexcept {
        auto rp = relativize(path, root);
        auto file = files->by_name(rp.path.string());
        if (file) {
            files_queue.push_back(file_info_t{file, rp.temp});
        }
    };

    errors_t errors;
    for (auto it = bfs::directory_iterator(dir); it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        bool is_dir = bfs::is_directory(child, ec);
        if (ec) {
            errors.push_back(scan_error_t{child, scan_context_t::path_type,  ec});
            continue;
        }

        if (is_dir) {
            dirs_queue.push_back(child);
            continue;
        }

        bool is_reg = bfs::is_regular_file(child, ec);
        if (ec) {
            errors.push_back(scan_error_t{child, scan_context_t::path_type,  ec});
            continue;
        }
        if (is_reg) {
            auto &child_path = child.path();
            if (!is_temporal(child_path)) {
                push(child_path);
            } else {
                auto modified_at = bfs::last_write_time(child_path, ec);
                if (ec) {
                    errors.push_back(scan_error_t{child_path, scan_context_t::modification_times,  ec});
                } else {
                    auto now = std::time(nullptr);
                    if (modified_at + config.temporally_timeout <= now) {
                        LOG_DEBUG(log, "removing outdated temporally {}", child_path.string());
                        bfs::remove(child_path, ec);
                        if (ec) {
                            errors.push_back(scan_error_t{child_path, scan_context_t::removing_temporal,  ec});
                        }
                    } else {
                        push(child_path);
                    }
                }
            }
            continue;
        }

    }

    if (!errors.empty()) {
        return std::move(errors);
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
        return errors_t{ scan_error_t{path, scan_context_t::file_size, ec}};
    }

    if (!info.temp) {
        if (sz != file->get_size()) {
            return changed_meta_t{info.file};
        }

        auto modified = bfs::last_write_time(path, ec);
        if (ec) {
            return errors_t{ scan_error_t{path, scan_context_t::modification_times, ec}};
        }
        if (modified != file->get_modified_s()) {
            return changed_meta_t{info.file};
        }
        return unchanged_meta_t{info.file};
    }

    if (sz != file->get_size()) {
        // ignore tmp file
        return true;
    }

    return incomplete_t{info.file};
}
