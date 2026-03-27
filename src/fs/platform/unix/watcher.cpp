// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "fs/platform/unix/watcher.h"

#if defined(SYNCSPIRIT_WATCHER_INOTIFY) || defined(SYNCSPIRIT_WATCHER_KQUEUE)

#include "fs/task/scan_dir.h"

using namespace syncspirit::fs::platform::unix;

void watcher_t::shutdown_finish() noexcept {
    for (auto it = watched_folders->begin(); it != watched_folders->end();) {
        auto &folder_id = it->first;
        auto &path = it->second.path_str;
        LOG_DEBUG(log, "unwatching {}", path);
        auto ec = unwatch_recurse(folder_id);
        if (ec) {
            LOG_WARN(log, "cannot unwatch '{}' : {}", path, ec.message());
        }
        it = watched_folders->erase(it);
    }
    parent_t::shutdown_finish();
}

void watcher_t::rename_self_descending(int parent_wd, std::string_view prev_path, std::string_view new_path) noexcept {
    using queue_t = std::pmr::list<int>;
    int sz_diff = static_cast<int>(new_path.size()) - static_cast<int>(prev_path.size());
    auto parent_path = std::string_view(path_map[parent_wd].path);
    for (auto &wd : subdir_map[parent_wd]) {
        auto &current_guard = path_map[wd];
        auto current_path = std::string_view(current_guard.path);
        auto ends_with = [&]() -> bool {
            if (current_path.size() < prev_path.size()) {
                return false;
            }
            auto rel_path = current_path.substr(current_path.size() - prev_path.size());
            return rel_path == prev_path;
        }();
        if (ends_with) {
            auto folder_path_sz = current_path.size() - prev_path.size();
            auto folder_path = parent_path.substr(0, folder_path_sz);
            auto number = 0;
            auto buff = std::array<std::byte, 1024>();
            auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
            auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
            auto queue = queue_t(allocator);
            queue.emplace_back(wd);
            while (!queue.empty()) {
                auto wd = queue.front();
                queue.pop_front();
                auto &guard = path_map[wd];
                auto prev_view = std::string_view(guard.path);
                auto tail_index = folder_path_sz + prev_path.size();
                auto tail = prev_view.substr(tail_index);
                auto new_sz = prev_view.size() + sz_diff;
                auto new_value = std::string();
                auto ex_path = std::string_view(guard.path);
                new_value.reserve(static_cast<std::size_t>(new_sz));
                new_value += folder_path;
                new_value += new_path;
                new_value += tail;
                path_to_wd.erase(prev_view);
                guard.path = std::move(new_value);
                path_to_wd.emplace(guard.path, wd);
                ++number;
                auto &children = subdir_map[wd];
                auto inserter = std::back_insert_iterator(queue);
                std::copy(children.begin(), children.end(), inserter);
            }
            LOG_DEBUG(log, "updated self & descendants ({}) '{}' -> '{}'", number, prev_path, new_path);
            break;
        }
    }
}

auto watcher_t::unwatch_recurse(std::string_view folder_id) noexcept -> sys::error_code {
    using queue_t = std::pmr::list<int>;
    auto buff = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    auto ec = sys::error_code{};
    auto &path = watched_folders->find(folder_id)->second.path_str;
    auto it_path = path_to_wd.find(path);
    if (it_path != path_to_wd.end()) {
        queue.emplace_back(path_to_wd[path]);
        while (!queue.empty()) {
            auto wd = queue.front();
            auto it_subdir = subdir_map.find(wd);
            auto it_guard = path_map.find(wd);
            if (it_subdir != subdir_map.end()) {
                auto &children = it_subdir->second;
                for (auto child_wd : children) {
                    queue.emplace_back(child_wd);
                }
                subdir_map.erase(it_subdir);
            }
            auto &path = it_guard->second.path;
            if (auto ec_rm = unwatch_dir(wd); ec_rm) {
                if (!ec) {
                    ec = ec_rm;
                } else {
                    LOG_ERROR(log, "cannot unwatch dir for '{}': {}", path, ec_rm.message());
                }
            } else {
                LOG_TRACE(log, "unwatched '{}'", path);
            }
            path_to_wd.erase(path);
            path_map.erase(it_guard);
            queue.pop_front();
        }
    }
    return ec;
}

void watcher_t::on_watch(message::watch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    auto path_str = std::string_view(path.native());
    LOG_TRACE(log, "on watch on '{}'", path_str);
    if (ready) {
        auto folder_info = folder_info_t(p.path, std::string(path_str));
        auto [it, inserted] =
            watched_folders->emplace(std::make_pair(std::string(p.folder_id), std::move(folder_info)));
        if (!inserted) {
            LOG_WARN(log, "folder '{}' on '{}' is already watched", p.folder_id, path_str);
        } else {
            auto &folder_id = it->first;
            auto [ec, _, wd] = watch_recurse(path_str, folder_id, -1);
            if (ec) {
                watched_folders->erase(it);
            }
            p.ec = ec;
        }
    }
}

void watcher_t::on_unwatch(message::unwatch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto it = watched_folders->find(p.folder_id);
    if (it != watched_folders->end()) {
        auto &path = it->second.path_str;
        LOG_DEBUG(log, "unwatching {}", path);
        auto it_wd = path_to_wd.find(path);
        assert(it_wd != path_to_wd.end());
        p.ec = unwatch_recurse(p.folder_id);
        watched_folders->erase(it);
    } else {
        LOG_WARN(log, "cannot unwatch folder '{}' as it has not been watched", p.folder_id);
    }
}

void watcher_t::notify(const fs::task::scan_dir_t &scan_dir) noexcept {
    auto parent_path = std::string_view(scan_dir.path.native());
    auto it = path_to_wd.find(parent_path);
    if (it != path_to_wd.end()) {
        auto &parent_wd = it->second;
        auto &folder_id = path_map[parent_wd].folder_id;
        for (auto &child : scan_dir.child_infos) {
            if (child.status.type() == bfs::file_type::directory) {
                auto path = std::string_view(child.path.native());
                if (path_to_wd.count(path) == 0) {
                    auto [ec, guard_ptr, wd] = watch_dir(path, folder_id, parent_wd);
                    if (ec) {
                        LOG_ERROR(log, "cannot watch {}: {}", path, ec.message());
                    }
                } else {
                    LOG_TRACE(log, "dir '{}' is already watched", path);
                }
            }
        }
    } else {
        LOG_WARN(log, "notification upon non-watched '{}'", parent_path);
    }
}

auto watcher_t::watch_dir(std::string_view path, std::string_view folder_id, int parent) noexcept -> watch_result_t {
    auto r = watch_result_t();
    auto path_str = path.data();
    if (path_to_wd.count(path_str)) {
        LOG_DEBUG(log, "dir '{}' is already watched", path_str);
        return r;
    }
    auto wr = watch_dir(path_str);
    if (!wr) {
        auto &ec = wr.assume_error();
        LOG_ERROR(log, "cannot watch dir for '{}': {}", path_str, ec.message());
        r.ec = ec;
    } else {
        r.wd = wr.assume_value();
        LOG_TRACE(log, "watching for '{}' (wd: {}, parent: {})", path_str, r.wd, parent);
        auto path_guard = path_guard_t{
            path_str,
            folder_id,
            parent,
        };
        auto [it, inserted] = path_map.emplace(r.wd, std::move(path_guard));
        assert(inserted);
        (void)inserted;
        r.guard = &it->second;
        path_to_wd.emplace(r.guard->path, r.wd);
        if (parent >= 0) {
            subdir_map[parent].emplace(r.wd);
        }
    }
    return r;
}

auto watcher_t::watch_recurse(std::string_view path, std::string_view folder_id, int parent_fd) noexcept
    -> watch_result_t {
    using item_t = std::pair<std::pmr::string, int>;
    using queue_t = std::pmr::list<item_t>;
    using ec_opt_t = std::optional<sys::error_code>;
    auto buff = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    queue.emplace_back(item_t{path, parent_fd});
    auto r = watch_result_t();
    while (!queue.empty()) {
        auto [path, parent_fd] = queue.front();
        auto sub_result = watch_dir(path, folder_id, parent_fd);
        auto &ec = sub_result.ec;
        if (r.ec) {
            r.ec = ec;
        }
        if (!ec && sub_result.guard) {
            auto it = bfs::directory_iterator(path, ec);
            if (ec) {
                LOG_WARN(log, "cannot list dir '{}': {}", path, ec.message());
            } else {
                for (; it != bfs::directory_iterator(); ++it) {
                    auto &child = *it;
                    auto ec = sys::error_code{};
                    auto status = bfs::symlink_status(child, ec);
                    auto &child_path = child.path().native();
                    if (ec) {
                        LOG_WARN(log, "cannot get child '{}' status : {}", child_path, ec.message());
                    } else {
                        if (status.type() == bfs::file_type::directory) {
                            queue.emplace_back(item_t{child_path, sub_result.wd});
                        }
                    }
                }
            }
        }
        queue.pop_front();
    }

    return r;
}
void watcher_t::forget(int wd) noexcept {
    auto it_guard = path_map.find(wd);
    if (it_guard != path_map.end()) {
        auto it_subdir = subdir_map.find(wd);
        auto &guard = it_guard->second;
        auto &path = guard.path;
        LOG_TRACE(log, "forgetting '{}'", path);
        if (it_subdir != subdir_map.end()) {
            auto &children = it_subdir->second;
            if (children.empty()) {
                LOG_WARN(log, "forgetting watching '{}' still having '{}' watched children", path, children.size());
            }
            subdir_map.erase(it_subdir);
        }
        if (guard.parent_fd >= 0) {
            auto &siblings = subdir_map[guard.parent_fd];
            auto removed = siblings.erase(wd);
            if (!removed) {
                LOG_WARN(log, "cannot remove '{}' from parent, is it orphaned?", path);
            }
        }
        path_to_wd.erase(path);
        path_map.erase(it_guard);
    }
}

#endif
