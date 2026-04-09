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
        LOG_DEBUG(log, "unwatching folder '{}' on {}", folder_id, path);
        auto ec = unwatch_folder(folder_id);
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

auto watcher_t::unwatch_recurse(std::string_view path) noexcept -> sys::error_code {
    using queue_t = std::pmr::list<int>;
    auto buff = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    auto ec = sys::error_code{};
    auto it_path = path_to_wd.find(path);
    if (it_path != path_to_wd.end()) {
        queue.emplace_back(path_to_wd[path]);
        while (!queue.empty()) {
            auto wd = queue.front();
            auto it_subdir = subdir_map.find(wd);
            auto it_guard = path_map.find(wd);
            if (it_subdir != subdir_map.end()) {
                auto &children = it_subdir->second;
                if (children.size()) {
                    for (auto child_wd : children) {
                        queue.emplace_front(child_wd);
                    }
                    continue;
                }
            }
            LOG_TRACE(log, "unwatching '{}'", path);
            auto ec_rm = unwatch_wd(wd);

            if (ec_rm) {
                if (!ec) {
                    ec = ec_rm;
                } else {
                    LOG_ERROR(log, "cannot unwatch '{}': {}", path, ec_rm.message());
                }
            }

            if (ec_rm && !ec) {
                ec = ec_rm;
            }
            queue.pop_front();
        }
    }
    return ec;
}

sys::error_code watcher_t::unwatch_wd(int wd) noexcept {
    auto it_guard = path_map.find(wd);
    auto &guard = it_guard->second;
    auto parent_wd = guard.parent_fd;
    if (parent_wd >= 0) {
        auto it_siblings = subdir_map.find(parent_wd);
        if (it_siblings != subdir_map.end()) {
            auto &children = it_siblings->second;
            auto it_self = children.find(wd);
            if (it_self != children.end()) {
                children.erase(it_self);
            }
        }
    }
    auto &path = guard.path;
    if (auto it_children = subdir_map.find(wd); it_children != subdir_map.end()) {
        auto &children = it_children->second;
        if (children.size()) {
            LOG_WARN(log, "unwatched '{}' still has watched {} children", path, children.size());
        } else {
            subdir_map.erase(it_children);
        }
    }
    auto ec = unwatch_path(wd, guard.file_type);
    path_to_wd.erase(path);
    path_map.erase(it_guard);

    return ec;
}

auto watcher_t::unwatch_folder(std::string_view folder_id) noexcept -> sys::error_code {
    auto &path = watched_folders->find(folder_id)->second.path_str;
    return unwatch_recurse(path);
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
            auto opt = watch_recurse(path_str, folder_id, -1);
            assert(opt.has_value());
            auto fd = *opt;
            if (fd < 0) {
                watched_folders->erase(it);
                p.ec = sys::error_code{errno, sys::system_category()};
            } else {
                p.ec = {};
            }
        }
    }
}

void watcher_t::on_unwatch(message::unwatch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto it = watched_folders->find(p.folder_id);
    if (it != watched_folders->end()) {
        auto &path = it->second.path_str;
        LOG_DEBUG(log, "unwatching folder '{}' on {}", p.folder_id, path);
        auto it_wd = path_to_wd.find(path);
        assert(it_wd != path_to_wd.end());
        p.ec = unwatch_folder(p.folder_id);
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
            auto type = child.status.type();
            if (type == bfs::file_type::directory) {
                auto path = std::string_view(child.path.native());
                if (path_to_wd.count(path) == 0) {
                    auto opt = watch_path(path, folder_id, type, parent_wd);
                    if (!opt) {
                        continue;
                    }
                    auto &[_, fd] = *opt;
                    if (fd < 0) {
                        LOG_ERROR(log, "cannot watch {}: {}", path, strerror(errno));
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

auto watcher_t::watch_path(std::string_view path, std::string_view folder_id, file_type_t type, int parent) noexcept
    -> watch_opt_t {
    auto path_str = path.data();
    if (path_to_wd.count(path_str)) {
        LOG_DEBUG(log, "dir '{}' is already watched", path_str);
        return {};
    }
    auto opt = watch_path(path_str, type);
    if (!opt) {
        return {};
    }
    auto fd = *opt;
    if (fd < 0) {
        LOG_ERROR(log, "cannot watch dir for '{}': {}", path_str, strerror(errno));
        return watch_result_t{nullptr, -1};
    }

    LOG_TRACE(log, "watching for '{}' (wd: {}, parent: {})", path_str, fd, parent);
    auto path_guard = path_guard_t{
        path_str,
        folder_id,
        type,
        parent,
    };
    auto [it, inserted] = path_map.emplace(fd, std::move(path_guard));
    assert(inserted);
    (void)inserted;
    auto guard_ptr = &it->second;
    path_to_wd.emplace(guard_ptr->path, fd);
    if (parent >= 0) {
        subdir_map[parent].emplace(fd);
    }
    return watch_result_t{guard_ptr, fd};
}

auto watcher_t::watch_recurse(std::string_view path, std::string_view folder_id, int parent_fd) noexcept
    -> std::optional<int> {
    static constexpr auto DIR = bfs::file_type::directory;
    struct item_t {
        std::pmr::string path;
        int parent_fd;
        bfs::file_type file_type;
    };
    using queue_t = std::pmr::list<item_t>;
    using ec_opt_t = std::optional<sys::error_code>;
    auto buff = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    queue.emplace_back(item_t{std::pmr::string(path, allocator), parent_fd, DIR});
    auto r = std::optional<int>{};
    while (!queue.empty()) {
        auto [path, parent_fd, type] = queue.front();
        queue.pop_front();
        auto opt = watch_path(path, folder_id, type, parent_fd);
        if (!opt) {
            continue;
        }
        auto &[guard_ptr, fd] = *opt;
        if (!r) {
            r = fd;
        }
        if (fd >= 0 && guard_ptr && type == DIR) {
            auto ec = sys::error_code{};
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
                        auto cp = std::pmr::string(child_path, allocator);
                        queue.emplace_back(item_t{std::move(cp), fd, status.type()});
                    }
                }
            }
        }
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
