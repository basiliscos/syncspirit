// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <sys/inotify.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <algorithm>
#include <memory_resource>
#include <list>
#include <optional>
#include "fs/fs_supervisor.h"
#include "fs/task/scan_dir.h"

using namespace syncspirit::fs::platform::linux;

void watcher_t::do_initialize(r::system_context_t *ctx) noexcept {
    auto inotify_fd = inotify_init();
    if (inotify_fd < 0) {
        LOG_ERROR(log, "inotify_init failed: {}", strerror(errno));
        return do_shutdown();
    } else {
        int flags = fcntl(inotify_fd, F_GETFL, 0);
        if (flags == -1) {
            LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
        } else {
            if (fcntl(inotify_fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
            } else {
                LOG_TRACE(log, "created inotify fd = {}", inotify_fd);
                auto sup = static_cast<fs::fs_supervisor_t *>(supervisor);
                auto ctx = static_cast<fs::fs_context_t *>(sup->context);
                auto cb = [](auto, void *data) { reinterpret_cast<watcher_t *>(data)->inotify_callback(); };
                io_guard = ctx->register_callback(inotify_fd, std::move(cb), this);
            }
        }
    }
    parent_t::do_initialize(ctx);
}

void watcher_t::shutdown_finish() noexcept {
    for (auto it = folder_map.begin(); it != folder_map.end();) {
        auto &folder_id = it->first;
        auto &path = it->second.path_str;
        LOG_DEBUG(log, "unwatching {}", path);
        auto ec = unwatch_recurse(folder_id);
        if (ec) {
            LOG_WARN(log, "cannot unwatch '{}' : {}", path, ec.message());
        }
        it = folder_map.erase(it);
    }
    assert(subdir_map.empty());
    assert(path_map.empty());
    auto fd = io_guard.fd;
    if (fd > 0) {
        io_guard = {};
        close(io_guard.fd);
        LOG_TRACE(log, "closing inotify fd = {}", fd);
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
        auto rel_path = current_path.substr(current_path.size() - prev_path.size());
        if (rel_path == prev_path) {
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

void watcher_t::inotify_callback() noexcept {
    using U = update_type_t;
    char buffer[1024 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    int length = ::read(io_guard.fd, buffer, sizeof(buffer));
    LOG_TRACE(log, "inotify callback, result = {}", io_guard.fd, length);
    if (length < 0) {
        LOG_ERROR(log, "cannot read: {}", strerror(errno));
    }

    // Process the events
    if (length) {
        using renamed_cookies_t = std::pmr::unordered_map<uint32_t, inotify_event *>;
        auto deadline = clock_t::local_time() + retension;
        char name_buff[PATH_MAX];
        auto name_ptr = name_buff;
        auto append_name = [&](std::string_view piece) {
            auto sz = piece.size();
            name_ptr -= sz;
            std::copy(piece.begin(), piece.end(), name_ptr);
        };

        auto cookie_buff = std::array<std::byte, 1024>();
        auto pool = std::pmr::monotonic_buffer_resource(cookie_buff.data(), cookie_buff.size());
        auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
        auto renamed_cookies = renamed_cookies_t(allocator);

        auto forward_update = [&](inotify_event *event, std::string prev_path, update_type_internal_t type) {
            auto filename = std::string_view(event->name);
            append_name(filename);
            auto tail = name_ptr;
            auto parent_wd = event->wd;
            auto parent_guard = &path_map[parent_wd];
            auto parent = parent_guard;
            auto folder_id = parent_guard->folder_id;
            *(--name_ptr) = '/';
            append_name(parent_guard->path);

            auto &folder_path = folder_map.find(folder_id)->second.path_str;
            auto sub_path_sz = parent_guard->path.size() - folder_path.size();
            tail -= sub_path_sz;
            auto rel_path = std::string_view(tail, sub_path_sz + filename.size());
            auto full_sz = (name_buff + sizeof(name_buff) - 2) - name_ptr;
            auto full_path = std::string_view(name_ptr, full_sz);
            auto is_dir = event->mask & IN_ISDIR;
            if (is_dir) {
                if (type & update_type::CREATED) {
                    watch_recurse(full_path, parent->folder_id, parent_wd);
                } else if (event->mask & IN_MOVED_TO) {
                    rename_self_descending(parent_wd, prev_path, rel_path);
                }
            }
            push(deadline, folder_id, rel_path, std::move(prev_path), static_cast<update_type_t>(type));
        };
        for (int i = 0; i < length;) {
            auto prev_name = std::string();
            name_ptr = name_buff + sizeof(name_buff) - 1;
            *name_ptr-- = 0;

            struct inotify_event *event = (struct inotify_event *)&buffer[i];
            LOG_TRACE(log, "event 0x{:x}, cookie: 0x{:x}, on '{}'", event->mask, event->cookie, event->name);
            if (event->len) {
                auto type = update_type_internal_t{0};
                if (event->mask & IN_CREATE) {
                    type = update_type::CREATED;
                } else if (event->mask & IN_DELETE) {
                    type = update_type::DELETED;
                } else if (event->mask & IN_MOVED_FROM) {
                    renamed_cookies.emplace(event->cookie, event);
                } else if (event->mask & IN_MOVED_TO) {
                    auto it = renamed_cookies.find(event->cookie);
                    if (it == renamed_cookies.end()) {
                        type = update_type::CREATED;
                    } else {
                        auto prev_event = it->second;
                        auto pn = std::string_view(prev_event->name);
                        auto &prev_parent_guard = path_map[prev_event->wd];
                        auto prev_parent_path = std::string_view(prev_parent_guard.path);
                        auto folder_id = prev_parent_guard.folder_id;
                        auto &folder_path = folder_map.find(folder_id)->second.path_str;
                        auto subpath_bytes = prev_parent_path.size() - folder_path.size();
                        if (subpath_bytes) {
                            --subpath_bytes; // skip trailing '/'
                        }
                        auto sub_path_sz = subpath_bytes + pn.size();
                        if (subpath_bytes) {
                            ++sub_path_sz;
                        };
                        prev_name.reserve(sub_path_sz + 1);
                        prev_name += prev_parent_path.substr(prev_parent_path.size() - subpath_bytes);
                        if (subpath_bytes) {
                            prev_name += '/';
                        };
                        prev_name += pn;

                        type = update_type::META;
                        renamed_cookies.erase(it);
                    }
                } else if (event->mask & IN_MODIFY) {
                    type = update_type::CONTENT;
                } else if (event->mask & IN_ATTRIB) {
                    type = update_type::META;
                }

                if (type) {
                    forward_update(event, std::move(prev_name), type);
                } else {
                    LOG_DEBUG(log, "ignoring event 0x{:x} on '{}'", event->mask, event->name);
                }
            }
            if (event->mask & IN_DELETE_SELF) {
                forget(event->wd);
            }
            if (event->mask & IN_Q_OVERFLOW) {
                LOG_ERROR(log, "event queue overflow");
            }
            i += sizeof(struct inotify_event) + event->len;
        }
        for (auto it = renamed_cookies.begin(); it != renamed_cookies.end(); ++it) {
            name_ptr = name_buff + sizeof(name_buff) - 1;
            *name_ptr-- = 0;
            forward_update(it->second, {}, update_type::DELETED);
        }
    }
}

auto watcher_t::watch_dir(std::string_view path, std::string_view folder_id, int parent) noexcept -> watch_result_t {
    static constexpr auto FLAGS = IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVE | IN_DELETE_SELF;
    auto path_str = path.data();
    auto wd = ::inotify_add_watch(io_guard.fd, path_str, FLAGS);
    auto ec = sys::error_code{};
    auto guard_ptr = (path_guard_t *)(nullptr);
    if (wd <= 0) {
        LOG_ERROR(log, "cannot do inotify_add_watch() for '{}': {}", path_str, strerror(errno));
        ec = sys::error_code{errno, sys::system_category()};
    } else {
        LOG_TRACE(log, "watching for '{}' ({}, parent: {})", path_str, wd, parent);
        auto path_guard = path_guard_t{
            path_str,
            folder_id,
            parent,
        };
        auto [it, inserted] = path_map.emplace(wd, std::move(path_guard));
        assert(inserted);
        (void)inserted;
        guard_ptr = &it->second;
        path_to_wd.emplace(guard_ptr->path, wd);
        if (parent >= 0) {
            subdir_map[parent].emplace(wd);
        }
    }
    return watch_result_t{ec, guard_ptr, wd};
}

auto watcher_t::unwatch_recurse(std::string_view folder_id) noexcept -> sys::error_code {
    using queue_t = std::pmr::list<int>;
    auto buff = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    auto ec = sys::error_code{};
    auto &path = folder_map.find(folder_id)->second.path_str;
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
        if (auto r = ::inotify_rm_watch(io_guard.fd, wd); r != 0) {
            if (!ec) {
                ec = sys::error_code{errno, sys::system_category()};
            } else {
                LOG_ERROR(log, "cannot do inotify_rm_watch() for '{}': {}", path, strerror(errno));
            }
        } else {
            LOG_TRACE(log, "unwatched '{}'", path);
        }
        path_to_wd.erase(path);
        path_map.erase(it_guard);
        queue.pop_front();
    }
    return ec;
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
    auto r = watch_result_t{{}, nullptr, -1};
    while (!queue.empty()) {
        auto [path, parent_fd] = queue.front();
        auto [ec, guard_ptr, wd] = watch_dir(path, folder_id, parent_fd);
        if (!std::get<0>(r)) {
            std::get<0>(r) = ec;
        }
        if (!ec) {
            if (std::get<2>(r) < 0) {
                std::get<2>(r) = wd;
            }
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
                            queue.emplace_back(item_t{child_path, wd});
                        }
                    }
                }
            }
        }
        queue.pop_front();
    }

    return r;
}

void watcher_t::on_watch(message::watch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto &path = p.path;
    auto path_str = std::string_view(path.native());
    LOG_TRACE(log, "on watch on '{}'", path_str);
    if (io_guard.fd) {
        auto folder_info = folder_info_t(p.path, std::string(path_str));
        auto [it, inserted] = folder_map.emplace(std::make_pair(std::string(p.folder_id), std::move(folder_info)));
        if (!inserted) {
            LOG_WARN(log, "folder '{}' on '{}' is already watched", p.folder_id, path_str);
        } else {
            auto &folder_id = it->first;
            auto [ec, _, wd] = watch_recurse(path_str, folder_id, -1);
            if (ec) {
                folder_map.erase(it);
            }
            p.ec = ec;
        }
    }
}

void watcher_t::on_unwatch(message::unwatch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto it = folder_map.find(p.folder_id);
    if (it != folder_map.end()) {
        auto &path = it->second.path_str;
        LOG_DEBUG(log, "unwatching {}", path);
        auto it_wd = path_to_wd.find(path);
        assert(it_wd != path_to_wd.end());
        p.ec = unwatch_recurse(p.folder_id);
        folder_map.erase(it);
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

#endif
