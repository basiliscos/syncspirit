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

fs_context_t::fs_context_t() noexcept
#ifdef SYNCSPIRIT_WATCHER_INOTIFY
    : async_flag{false}
#elif SYNCSPIRIT_WATCHER_WIN32
    : async_event{nullptr}
#endif
{
    log = utils::get_logger("fs");
#ifdef SYNCSPIRIT_WATCHER_INOTIFY
    async_pipes[0] = async_pipes[1] = -1;
    int fds[2];
    auto r = pipe(fds);
    if (r == 0) {
        for (int i = 0; i < 2; ++i) {
            auto fd = fds[i];
            fcntl(fd, F_SETFD, FD_CLOEXEC);
            fcntl(fd, F_SETFD, O_NONBLOCK);
            async_pipes[i] = fd;
        }
    } else {
        LOG_CRITICAL(log, "cannot pipe(): {}", strerror(errno));
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
    for (int i = 0; i < 2; ++i) {
        close(async_pipes[i]);
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

void fs_context_t::wait_next_event() noexcept {
    using namespace std::chrono;
    using namespace std::chrono_literals;

#if SYNCSPIRIT_WATCHER_INOTIFY
    struct timeval timeout;

    if (!timer_nodes.empty()) {
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;
        auto &next = timer_nodes.front().deadline;
        auto delta = next - clock_t::now();
        auto secs_dur = duration_cast<seconds>(delta);
        auto secs = secs_dur.count();
        if (secs >= 0) {
            timeout.tv_sec = secs;
            auto u_dur = duration_cast<microseconds>(delta);
            auto u = u_dur.count() - secs * 1'000'000;
            if (u > 0) {
                timeout.tv_usec = u;
            }
        }
    } else {
        timeout.tv_usec = 0;
        timeout.tv_sec = 60;
    }

    fd_set read_fds;
    auto max_fd = async_pipes[0];
    FD_ZERO(&read_fds);
    FD_SET(max_fd, &read_fds);

    auto r = ::select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (r == -1) {
        LOG_WARN(log, "select() failed: {}", strerror(errno));
    } else if (r) {
        char dummy[4];
        r = ::read(max_fd, &dummy, sizeof(dummy));
        if (r < 0) {
            LOG_WARN(log, "read() failed: {}", strerror(errno));
        }
        async_flag.store(true, std::memory_order_release);
    }
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
