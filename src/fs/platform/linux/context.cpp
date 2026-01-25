// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "context.h"

#if SYNCSPIRIT_WATCHER_INOTIFY

#include <fcntl.h>

using namespace syncspirit::fs::platform::linux;

using io_ctx = platform_context_t::io_context_t;
using guard_t = platform_context_t::io_guard_t;

io_ctx::io_context_t(platform_context_t::io_callback_t callback_, void *data_, int index_)
    : cb(std::move(callback_)), data{data_}, index{index_} {}

guard_t::io_guard_t() : fd{-1} {};
guard_t::io_guard_t(platform_context_t *ctx_, int fd_) : ctx{ctx_}, fd{fd_} {};
guard_t::io_guard_t(io_guard_t &&other) : fd{-1} {
    std::swap(ctx, other.ctx);
    std::swap(fd, other.fd);
}
guard_t::~io_guard_t() {
    if (fd >= 0 && ctx) {
        auto log = utils::get_logger("fs");
        auto code = epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        LOG_DEBUG(log, "epoll_ctl(DEL), fd = {}, code = {}", fd, code);
        auto it = ctx->io_callbacks.find(fd);
        if (it != ctx->io_callbacks.end()) {
            ctx->io_callbacks.erase(it);
        }
    }
}
guard_t &guard_t::operator=(io_guard_t &&other) noexcept {
    std::swap(ctx, other.ctx);
    std::swap(fd, other.fd);
    return *this;
}
guard_t::operator bool() const { return fd >= 0; }

static void async_cb(int fd, void *data) {
    auto ctx = reinterpret_cast<platform_context_t *>(data);
    char dummy[4];
    bool do_read = true;
    while (do_read) {
        auto r = ::read(fd, &dummy, sizeof(dummy));
        do_read = r > 0;
        if (r < 0) {
            if (errno != EAGAIN) {
                LOG_WARN(ctx->log, "read() failed: {}", strerror(errno));
            }
        }
    }
    ctx->async_flag.store(false, std::memory_order_release);
}

platform_context_t::platform_context_t() noexcept : async_flag{false} {
    async_pipes[0] = async_pipes[1] = epoll_fd = -1;

    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        LOG_CRITICAL(log, "cannot epoll_create1(): {}", strerror(errno));
    } else {
        int fds[2];
        auto r = pipe(fds);
        if (r == 0) {
            for (int i = 0; i < 2; ++i) {
                auto fd = fds[i];
                int flags = fcntl(fd, F_GETFL, 0);
                if (flags == -1) {
                    LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                } else {
                    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
                        LOG_CRITICAL(log, "cannot fcntl(): {}", strerror(errno));
                    } else {
                        async_pipes[i] = fd;
                    }
                }
            }

            async_guard = register_callback(async_pipes[0], async_cb, this);
        } else {
            LOG_CRITICAL(log, "cannot pipe(): {}", strerror(errno));
        }
    }
}

platform_context_t::~platform_context_t() {
    async_guard = {};
    for (int i = 0; i < 2; ++i) {
        if (async_pipes[i] >= 0) {
            close(async_pipes[i]);
        }
    }
    if (epoll_fd >= 0) {
        close(epoll_fd);
    }
}

auto platform_context_t::register_callback(int fd, io_callback_t callback, void *data) -> io_guard_t {
    auto &event = events.emplace_back(epoll_event{});
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        LOG_ERROR(log, "cannot epoll_ctl(): {}", strerror(errno));
        return {};
    } else {
        LOG_DEBUG(log, "epoll_ctl(ADD), fd = {}", fd);
        auto index = static_cast<int>(events.size());
        io_callbacks.emplace(fd, io_context_t(callback, data, index));
        return io_guard_t(this, fd);
    }
}

void platform_context_t::notify() noexcept {
    if (!async_flag.load(std::memory_order_acquire)) {
        async_flag.store(true, std::memory_order_release);
        write(async_pipes[1], &(async_pipes[1]), 1);
    }
}

void platform_context_t::wait_next_event() noexcept {
    using namespace std::chrono;
    using namespace std::chrono_literals;

    auto timeout = determine_wait_ms();
    int i = 0;
    for (auto &[fd, i_ctx] : io_callbacks) {
        auto &event = events[i++];
        event.data.fd = fd;
        event.events = EPOLLIN;
    }

    auto r = ::epoll_wait(epoll_fd, events.data(), static_cast<int>(events.size()), timeout);
    bool timed_out = r == 0;
    if (r == -1) {
        LOG_WARN(log, "epoll_wait() failed: {}", strerror(errno));
    } else if (r) {
        for (auto it = events.begin(); r && it != events.end(); ++it) {
            auto &event = *it;
            if (event.events & EPOLLIN) {
                --r;
                auto fd = event.data.fd;
                auto &ctx = io_callbacks.at(fd);
                ctx.cb(fd, ctx.data);
            }
        }
    }
}

#endif
