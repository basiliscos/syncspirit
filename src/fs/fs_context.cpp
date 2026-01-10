// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "fs_context.hpp"
#include "utils/log.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <errno.h>
#include <string.h>
#endif

using namespace syncspirit::fs;
using namespace rotor;

namespace {
namespace to {
struct state {};
struct queue {};
struct poll_duration {};
struct inbound_queue {};
} // namespace to
} // namespace

template <> auto &supervisor_t::access<to::state>() noexcept { return state; }
template <> auto &supervisor_t::access<to::queue>() noexcept { return queue; }
template <> auto &supervisor_t::access<to::poll_duration>() noexcept { return poll_duration; }
template <> auto &supervisor_t::access<to::inbound_queue>() noexcept { return inbound_queue; }

#ifdef SYNCSPIRIT_WATCHER_INOTIFY
fs_context_t::io_context_t::io_context_t(fs_context_t::io_callback_t callback_, void *data_, int index_)
    : cb(std::move(callback_)), data{data_}, index{index_} {}
#endif

#ifdef SYNCSPIRIT_WATCHER_INOTIFY
fs_context_t::io_guard_t::io_guard_t() : fd{0} {};
fs_context_t::io_guard_t::io_guard_t(fs_context_t *ctx_, int fd_) : ctx{ctx_}, fd{fd_} {};
fs_context_t::io_guard_t::io_guard_t(io_guard_t &&other) : fd{0} {
    std::swap(ctx, other.ctx);
    std::swap(fd, other.fd);
}
fs_context_t::io_guard_t::~io_guard_t() {
    if (fd && ctx) {
        if (epoll_ctl(ctx->epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            auto log = utils::get_logger("fs");
            LOG_CRITICAL(log, "cannot epoll_ctl(del): {}", strerror(errno));
        }
        auto it = ctx->io_callbacks.find(fd);
        if (it != ctx->io_callbacks.end()) {
            ctx->io_callbacks.erase(it);
        }
    }
}
fs_context_t::io_guard_t &fs_context_t::io_guard_t::operator=(io_guard_t &&other) {
    std::swap(ctx, other.ctx);
    std::swap(fd, other.fd);
    return *this;
}
fs_context_t::io_guard_t::operator bool() const { return fd; }
#endif

#ifdef SYNCSPIRIT_WATCHER_INOTIFY
static void async_cb(int fd, void *data) {
    auto ctx = reinterpret_cast<fs_context_t *>(data);
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
#endif

fs_context_t::fs_context_t() noexcept
#ifdef SYNCSPIRIT_WATCHER_INOTIFY
    : async_flag{false}
#elif SYNCSPIRIT_WATCHER_WIN32
    : async_event{nullptr}
#endif
{
    log = utils::get_logger("fs");
#ifdef SYNCSPIRIT_WATCHER_INOTIFY
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

#elif SYNCSPIRIT_WATCHER_WIN32
    async_event = ::CreateEvent(nullptr, false, false, nullptr);
    if (!async_event) {
        LOG_CRITICAL(log, "cannot CreateEvent(): {}", ::GetLastError());
    }
#endif
}

fs_context_t::~fs_context_t() {
#if SYNCSPIRIT_WATCHER_INOTIFY
    async_guard = {};
    for (int i = 0; i < 2; ++i) {
        if (async_pipes[i] >= 0) {
            close(async_pipes[i]);
        }
    }
    if (epoll_fd >= 0) {
        close(epoll_fd);
    }
#elif SYNCSPIRIT_WATCHER_WIN32
    if (async_event) {
        ::CloseHandle(async_event);
    }
#endif
}

void fs_context_t::run() noexcept {
    using std::chrono::duration_cast;
    using time_units_t = std::chrono::microseconds;

    auto &root_sup = *get_supervisor();
    auto condition = [&]() -> bool { return root_sup.access<to::state>() != state_t::SHUT_DOWN; };
    auto &queue = root_sup.access<to::queue>();
    auto &inbound = root_sup.access<to::inbound_queue>();
    auto &poll_duration = root_sup.access<to::poll_duration>();

    auto try_pop_inbound = [&]() -> void {
        message_base_t *ptr;
        while (inbound.pop(ptr)) {
            queue.emplace_back(ptr, false);
        }
    };

    auto total_us = poll_duration.total_microseconds();
    auto delta = time_units_t{total_us};
    while (condition()) {
        root_sup.do_process();
        if (condition()) {
            try_pop_inbound();
            if (total_us && queue.empty()) {
                auto dealine = clock_t::now() + delta;
                if (!timer_nodes.empty()) {
                    dealine = std::min(dealine, timer_nodes.front().deadline);
                }
                // fast stage, indirect spin-lock, cpu consuming
                while (queue.empty() && (clock_t::now() < dealine)) {
                    try_pop_inbound();
                }
            }
            if (queue.empty()) {
                wait_next_event();
            }
            update_time();
        }
    }
    root_sup.do_process();
}

#if SYNCSPIRIT_WATCHER_INOTIFY
auto fs_context_t::register_callback(int fd, io_callback_t callback, void *data) -> io_guard_t {
    auto &event = events.emplace_back(epoll_event{});
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
        LOG_ERROR(log, "cannot epoll_ctl(): {}", strerror(errno));
        return {};
    } else {
        auto index = static_cast<int>(events.size());
        io_callbacks.emplace(fd, io_context_t(callback, data, index));
        return io_guard_t(this, fd);
    }
}
#endif

bool fs_context_t::wait_next_event() noexcept {
    using namespace std::chrono;
    using namespace std::chrono_literals;

#if SYNCSPIRIT_WATCHER_INOTIFY
    // auto timeout = int{60'000};
    auto timeout = int{1'000};
    // auto timeout = int{5'000};

    if (!timer_nodes.empty()) {
        auto &next = timer_nodes.front().deadline;
        auto delta = next - clock_t::now();
        auto milli_dur = duration_cast<milliseconds>(delta);
        auto milli = milli_dur.count() + 1; // to round up
        timeout = std::max(0, static_cast<int>(milli));
    }

    for (auto &event : events) {
        event.events = EPOLLIN;
    }

    auto r = ::epoll_wait(epoll_fd, events.data(), static_cast<int>(events.size()), timeout);
    bool timed_out = r == 0;
    // LOG_ERROR(log, "zzz r = {}, timeout = {}", r, timeout);
    if (r == -1) {
        LOG_WARN(log, "epoll_wait() failed: {}", strerror(errno));
    } else if (r) {
        for (auto &event : events) {
            if (event.events & EPOLLIN) {
                auto fd = event.data.fd;
                auto &ctx = io_callbacks.at(fd);
                ctx.cb(fd, ctx.data);
            }
        }
    }
    return timed_out;
#elif SYNCSPIRIT_WATCHER_WIN32
    auto wait_millisecs = std::uint32_t{60'000};
    if (!timer_nodes.empty()) {
        auto &next = timer_nodes.front().deadline;
        auto delta = next - clock_t::now();
        auto milli_dur = duration_cast<milliseconds>(delta);
        auto milli = milli_dur.count();
        if (milli > 0) {
            wait_millisecs = static_cast<std::uint32_t>(milli);
        }
    }
    auto r = ::WaitForSingleObject(async_event, wait_millisecs);
    if (r == WAIT_OBJECT_0) {
        ::ResetEvent(async_event);
    } else if (r == WAIT_TIMEOUT) {
        // NO-OP
    } else {
        LOG_WARN(log, "WaitFor*Object failed: {}", ::GetLastError());
    }
#else
    std::unique_lock<std::mutex> lock(mutex);
    auto &root_sup = *get_supervisor();
    auto &inbound = root_sup.access<to::inbound_queue>();
    auto predicate = [&]() { return !inbound.empty(); };
    // wait notification, do not consume CPU
    auto deadline = !timer_nodes.empty() ? timer_nodes.front().deadline : clock_t::now() + 1min;
    cv.wait_until(lock, deadline, predicate);
#endif
}
