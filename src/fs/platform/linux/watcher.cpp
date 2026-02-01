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
        auto it_root = root_map.find(folder_id);
        assert(it_root != root_map.end());
        auto root_fd = root_map[folder_id];
        auto ec = unwatch_recurse(folder_id);
        if (ec) {
            LOG_WARN(log, "cannot unwatch '{}' : {}", path, ec.message());
        }
        root_map.erase(it_root);
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

        auto forward = [&](inotify_event *event, std::string prev_path, update_type_internal_t type) {
            auto filename = std::string_view(event->name);
            append_name(filename);
            auto tail = name_ptr;
            auto parent_fd = event->wd;
            auto guard = &path_map[parent_fd];
            auto parent = guard;
            auto folder_id = guard->folder_id;
            *(--name_ptr) = '/';
            append_name(guard->path);

            auto &folder_path = folder_map.find(folder_id)->second.path_str;
            auto sub_path_sz = guard->path.size() - folder_path.size();
            tail -= sub_path_sz;
            auto rel_path = std::string_view(tail, sub_path_sz + filename.size());
            push(deadline, folder_id, rel_path, std::move(prev_path), static_cast<update_type_t>(type));
            if (event->mask & IN_CREATE) {
                auto full_sz = (name_buff + sizeof(name_buff) - 2) - name_ptr;
                auto full_path = std::string_view(name_ptr, full_sz);
                try_watch_recurse(full_path, *parent, parent_fd);
            }
        };
        for (int i = 0; i < length;) {
            auto prev_name = std::string();
            name_ptr = name_buff + sizeof(name_buff) - 1;
            *name_ptr-- = 0;

            struct inotify_event *event = (struct inotify_event *)&buffer[i];
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
                }
                if (event->mask & IN_MODIFY) {
                    type = update_type::CONTENT;
                }
                if (event->mask & IN_ATTRIB) {
                    type = update_type::META;
                }
                if (type) {
                    forward(event, std::move(prev_name), type);
                } else {
                    LOG_DEBUG(log, "ignoring event 0x{:x} on '{}'", event->mask, event->name);
                }
            }
            i += sizeof(struct inotify_event) + event->len;
        }
        for (auto it = renamed_cookies.begin(); it != renamed_cookies.end(); ++it) {
            name_ptr = name_buff + sizeof(name_buff) - 1;
            *name_ptr-- = 0;
            forward(it->second, {}, update_type::DELETED);
        }
    }
}

void watcher_t::try_watch_recurse(std::string_view full_name, const path_guard_t &parent_guard,
                                  int parent_fd) noexcept {
    struct stat info;
    if (lstat(full_name.data(), &info) != 0) {
        LOG_WARN(log, "cannot lstat() for '{}': {}", full_name, strerror(errno));
        return;
    }
    if (S_ISDIR(info.st_mode)) {
        watch_dir(full_name, parent_guard.folder_id, parent_fd);
    }
}

auto watcher_t::watch_dir(std::string_view path, std::string_view folder_id, int parent) noexcept -> watch_result_t {
    auto path_str = path.data();
    auto wd = ::inotify_add_watch(io_guard.fd, path_str, IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVE);
    auto ec = sys::error_code{};
    if (wd <= 0) {
        LOG_ERROR(log, "cannot do inotify_add_watch() for '{}': {}", path_str, strerror(errno));
        ec = sys::error_code{errno, sys::system_category()};
    } else {
        auto path_guard = path_guard_t{
            path_str,
            folder_id,
            parent,
        };
        LOG_TRACE(log, "watching for '{}' ({}, parent: {})", path_str, wd, parent);
        path_map[wd] = std::move(path_guard);
        if (parent) {
            subdir_map[parent].emplace_back(wd);
        }
    }
    return watch_result_t{ec, wd};
}

auto watcher_t::unwatch_recurse(std::string_view folder_id) noexcept -> sys::error_code {
    using queue_t = std::pmr::list<int>;
    using ec_opt_t = std::optional<sys::error_code>;
    auto buff = std::array<std::byte, 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto queue = queue_t(allocator);
    auto ec = sys::error_code{};
    queue.emplace_back(root_map[folder_id]);
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
        path_map.erase(it_guard);
        queue.pop_front();
    }
    return ec;
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
            using item_t = std::pair<std::pmr::string, int>;
            using queue_t = std::pmr::list<item_t>;
            using ec_opt_t = std::optional<sys::error_code>;
            auto buff = std::array<std::byte, 1024>();
            auto pool = std::pmr::monotonic_buffer_resource(buff.data(), buff.size());
            auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
            auto queue = queue_t(allocator);
            queue.emplace_back(item_t{path_str, 0});
            auto &folder_id = it->first;
            auto result = ec_opt_t{};
            auto root_fd = int{-1};

            while (!queue.empty()) {
                auto [path, parent_fd] = queue.front();
                auto [ec, fd] = watch_dir(path, folder_id, parent_fd);
                if (!result) {
                    result = ec;
                }
                if (!ec) {
                    if (root_fd < 0) {
                        root_fd = fd;
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
                                    queue.emplace_back(item_t{child_path, fd});
                                }
                            }
                        }
                    }
                }
                queue.pop_front();
            }
            if (*result) {
                folder_map.erase(it);
            } else {
                root_map.emplace(folder_id, root_fd);
            }
            p.ec = *result;
        }
    }
}

void watcher_t::on_unwatch(message::unwatch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto it = folder_map.find(p.folder_id);
    if (it != folder_map.end()) {
        LOG_DEBUG(log, "unwatching {}", it->second.path_str);
        auto it_root = root_map.find(p.folder_id);
        assert(it_root != root_map.end());
        auto root_fd = root_map[p.folder_id];
        p.ec = unwatch_recurse(p.folder_id);
        root_map.erase(it_root);
        folder_map.erase(it);
    } else {
        LOG_WARN(log, "cannot unwatch folder '{}' as it has been watched", p.folder_id);
    }
}

#endif
