// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "file.h"
#include "utils.h"
#include "utils/log.h"
#include "utils/format.hpp"
#include "utils/path_view.hpp"
#include "fs_proxy.h"
#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::fs;

using boost::nowide::narrow;

auto file_t::open_write(fs_proxy_t &fs_proxy, const utils::poly_path_view_t &model_path,
                        std::uint64_t file_size) noexcept -> outcome::result<file_t> {
    auto path = file_size > 0 ? model_path.make_temporal() : model_path.clone();
    auto result = fs_proxy.open_write(file_size > 0 ? model_path.make_temporal() : model_path, file_size);
    if (result.has_error()) {
        return result.assume_error();
    }

    auto &file = result.assume_value();
    return file_t(std::move(file), path.detach(), file_size);
}

auto file_t::open_read(const utils::poly_path_view_t &path) noexcept -> outcome::result<file_t> {
    auto r = utils::io_stream_t::open_read(path);
    if (!r) {
        return r.assume_error();
    }
    return file_t(std::move(r.assume_value()), path.detach());
}

file_t::file_t() noexcept {};

file_t::file_t(utils::io_stream_t backend_, utils::path_t path_, std::uint64_t file_size_) noexcept
    : backend{new utils::io_stream_t(std::move(backend_))}, path{std::move(path_)}, file_size{file_size_} {}

file_t::file_t(utils::io_stream_t backend_, utils::path_t path_) noexcept
    : backend{new utils::io_stream_t(std::move(backend_))}, path{std::move(path_)}, file_size{0} {}

file_t::file_t(file_t &&other) noexcept : backend{nullptr} { *this = std::move(other); }

file_t &file_t::operator=(file_t &&other) noexcept {
    std::swap(backend, other.backend);
    std::swap(path, other.path);
    std::swap(file_size, other.file_size);
    return *this;
}

file_t::~file_t() {
    if (backend && file_size) {
        auto log = utils::get_logger("fs.file");
        log->warn("closing file via d-tor '{}'", path);
        backend.reset();
    }
}

const utils::path_t &file_t::get_path() const noexcept { return path; }

auto file_t::finalize(fs_proxy_t *fs_proxy, int64_t modification_s, const utils::poly_path_view_t &local_name) noexcept
    -> outcome::result<void> {
    assert(backend && file_size && "close has sense for r/w mode");
    backend.reset();

    assert(!local_name.empty());
    auto ec = fs_proxy->rename(path, local_name);

    if (modification_s) {
        ec = fs_proxy->last_write_time(local_name, modification_s);
        if (ec) {
            return ec;
        }
    }

    return outcome::success();
}

bool file_t::has_backend() const noexcept { return backend.get(); }

auto file_t::remove(fs_proxy_t &fs_proxy, const utils::allocator_t &allocator) noexcept -> outcome::result<void> {
    backend.reset();

    return fs_proxy.remove(path.get_view(allocator));
}

auto file_t::read(std::uint64_t offset, std::uint64_t size) const noexcept -> outcome::result<utils::bytes_t> {
    auto pos_opt = backend->get_position();
    if (!pos_opt) {
        return pos_opt.assume_error();
    }
    auto pos = pos_opt.assume_value();
    if (pos != offset) {
        auto r = backend->set_position(offset);
        if (!r) {
            return r.assume_error();
        }
    }

    return backend->read_bytes(size);
}

auto file_t::write(fs_proxy_t &fs_proxy, uint64_t offset, utils::bytes_view_t data) noexcept -> outcome::result<void> {
    auto pos_opt = backend->get_position();
    if (!pos_opt) {
        return pos_opt.assume_error();
    }
    auto pos = pos_opt.assume_value();
    if (pos != offset) {
        auto r = backend->set_position(offset);
        if (!r) {
            return r.assume_error();
        }
    }

    if (auto ec = fs_proxy.write(path, *backend, data); ec) {
        return ec;
    }
    return outcome::success();
}

auto file_t::copy(fs_proxy_t &fs_proxy, std::uint64_t my_offset, const file_t &from, std::uint64_t source_offset,
                  std::uint64_t size) noexcept -> outcome::result<void> {
    auto in_opt = from.read(source_offset, size);
    if (!in_opt) {
        return in_opt.assume_error();
    }

    auto &in = in_opt.assume_value();
    return write(fs_proxy, my_offset, in);
}
