// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-config.h"
#include "fs_proxy.h"
#include "updates_mediator.h"
#include "utils.h"
#include <boost/nowide/convert.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace syncspirit::fs;

fs_proxy_t::fs_proxy_t(updates_mediator_t &updates_mediator_, const pt::ptime &deadline_) noexcept
    : updates_mediator{updates_mediator_}, deadline{deadline_} {}

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SS_STAT_FN(PATH, BUFF) _wstat64((PATH).native().data(), (BUFF))
#define SS_STAT_BUFF struct __stat64
#else
#define SS_STAT_FN(PATH, BUFF) stat((PATH).native().data(), (BUFF))
#define SS_STAT_BUFF struct stat
#endif

auto fs_proxy_t::open_write(const bfs::path &path, std::uint64_t file_size) noexcept
    -> outcome::result<utils::io_stream_t> {
    auto [file, resized, created] = utils::io_stream_t::open_write(path, file_size);

    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }

    if (created) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.parent_path(), {}, deadline);
#endif
        ++mediator_updates;
    }
    if (resized) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return outcome::success(std::move(file));
}

sys::error_code fs_proxy_t::rename(const bfs::path &from, const bfs::path &to) noexcept {
    auto ec = sys::error_code();
    bfs::rename(from, to, ec);
    if (!ec) {
        updates_mediator.mask(to, from, deadline);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(to.parent_path(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::last_write_time(const bfs::path &path, std::int64_t modification_s) noexcept {
    auto ec = sys::error_code();
    bfs::last_write_time(path, from_unix(modification_s), ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::remove(const bfs::path &path) noexcept {
    sys::error_code ec;
    bfs::remove_all(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.parent_path(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::remove_file(const bfs::path &path) noexcept {
    sys::error_code ec;
    bfs::remove(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.parent_path(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::write(const bfs::path &path, utils::io_stream_t &stream,
                                  utils::bytes_view_t data) noexcept {
    if (!stream.write(data.data(), data.size())) {
        return sys::errc::make_error_code(sys::errc::io_error);
    }
    updates_mediator.mask(path, {}, deadline);
    return {};
}

sys::error_code fs_proxy_t::set_perms(const bfs::path &path, std::uint32_t permissions) noexcept {
    auto ec = sys::error_code();
    bfs::permissions(path, static_cast<bfs::perms>(permissions), ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::create_link(const bfs::path &target, const bfs::path &path) noexcept {
    auto ec = sys::error_code();
    bfs::create_symlink(target, path, ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

sys::error_code fs_proxy_t::create_directories(const bfs::path &path) noexcept {
    auto ec = sys::error_code();
    bfs::create_directories(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.parent_path(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}
