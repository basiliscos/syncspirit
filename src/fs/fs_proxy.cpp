// SPDX-License-Identifier: GPL-3.0-or-later.
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-config.h"
#include "fs_proxy.h"
#include "updates_mediator.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"

using namespace syncspirit::fs;

fs_proxy_t::fs_proxy_t(updates_mediator_t &updates_mediator_, const pt::ptime &deadline_) noexcept
    : updates_mediator{updates_mediator_}, deadline{deadline_} {}

auto fs_proxy_t::open_write(const utils::poly_path_view_t &path, std::uint64_t file_size) noexcept
    -> outcome::result<utils::io_stream_t> {
    auto r = utils::io_stream_t::open_write(path, file_size);
    if (!r) {
        return r.assume_error();
    }
    auto &[file, resized, created] = r.assume_value();

    auto empty_view = utils::make_empty_view(path.get_allocator());
    if (created) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, empty_view, deadline);
#else
        updates_mediator.mask(path.get_parent(), empty_view, deadline);
#endif
        ++mediator_updates;
    }
    if (resized) {
        updates_mediator.mask(path, empty_view, deadline);
        ++mediator_updates;
    }
    return outcome::success(std::move(file));
}

std::error_code fs_proxy_t::rename(const utils::path_base_t &from, const utils::poly_path_view_t &to) noexcept {
    auto ec = std::error_code();
    utils::rename(from, to, ec);
    if (!ec) {
        updates_mediator.mask(to, from, deadline);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(to.get_parent(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::last_write_time(const utils::poly_path_view_t &path, std::int64_t modification_s) noexcept {
    auto ec = std::error_code();
    utils::last_write_time(path, modification_s, ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::remove(const utils::poly_path_view_t &path) noexcept {
    std::error_code ec;
    utils::remove_all(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.get_parent(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::remove_file(const utils::poly_path_view_t &path) noexcept {
    std::error_code ec;
    utils::remove_file(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.get_parent(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::write(const utils::path_base_t &path, utils::io_stream_t &stream,
                                  utils::bytes_view_t data) noexcept {
    if (!stream.write(data.data(), data.size())) {
        return std::make_error_code(std::errc::io_error);
    }
    updates_mediator.mask(path, {}, deadline);
    return {};
}

std::error_code fs_proxy_t::set_perms(const utils::poly_path_view_t &path, std::uint32_t permissions) noexcept {
    auto ec = std::error_code();
    utils::chmod(path, permissions, ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::create_link(const utils::path_base_t &target, const utils::path_base_t &path) noexcept {
    auto ec = std::error_code();
    utils::create_symlink(target, path, ec);
    if (!ec) {
        updates_mediator.mask(path, {}, deadline);
        ++mediator_updates;
    }
    return ec;
}

std::error_code fs_proxy_t::create_directories(const utils::poly_path_view_t &path) noexcept {
    auto ec = std::error_code();
    utils::create_directories(path, ec);
    if (!ec) {
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
        updates_mediator.mask(path, {}, deadline);
#else
        updates_mediator.mask(path.get_parent(), {}, deadline);
#endif
        ++mediator_updates;
    }
    return ec;
}
