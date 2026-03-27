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
                auto ctx = static_cast<platform_context_t *>(sup->context);
                auto cb = [](auto, void *data) { reinterpret_cast<watcher_t *>(data)->inotify_callback(); };
                inotify_guard = ctx->register_callback(inotify_fd, std::move(cb), this);
                ready = true;
            }
        }
    }
    parent_t::do_initialize(ctx);
}

void watcher_t::shutdown_finish() noexcept {
    parent_t::shutdown_finish();
    assert(subdir_map.empty());
    assert(path_map.empty());
    auto fd = inotify_guard.fd;
    if (fd > 0) {
        inotify_guard = {};
        close(fd);
        LOG_TRACE(log, "closing inotify fd = {}", fd);
    }
    return;
}

auto watcher_t::unwatch_dir(int wd) noexcept -> sys::error_code {
    if (auto r = ::inotify_rm_watch(inotify_guard.fd, wd); r != 0) {
        return sys::error_code{errno, sys::system_category()};
    }
    return {};
}

auto watcher_t::watch_dir(std::string_view path) noexcept -> outcome::result<int> {
    static constexpr auto FLAGS = IN_MODIFY | IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MOVE | IN_DELETE_SELF;
    auto wd = ::inotify_add_watch(inotify_guard.fd, path.data(), FLAGS);
    if (wd <= 0) {
        return sys::error_code{errno, sys::system_category()};
    }
    return outcome::success(wd);
}

void watcher_t::inotify_callback() noexcept {
    using U = update_type_t;
    char buffer[1024 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    int length = ::read(inotify_guard.fd, buffer, sizeof(buffer));
    LOG_TRACE(log, "inotify callback, result = {}", inotify_guard.fd, length);
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

            auto &folder_path = watched_folders->find(folder_id)->second.path_str;
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
                        auto &folder_path = watched_folders->find(folder_id)->second.path_str;
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

#endif
