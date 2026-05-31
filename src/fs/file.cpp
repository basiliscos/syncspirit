// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "file.h"
#include "utils.h"
#include "utils/log.h"
#include "fs_proxy.h"
#include <errno.h>
#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>
#include <boost/nowide/convert.hpp>

using namespace syncspirit::fs;

using boost::nowide::narrow;

auto file_t::open_write(fs_proxy_t &fs_proxy, const bfs::path &model_path, std::uint64_t file_size) noexcept
    -> outcome::result<file_t> {
    auto path = file_size > 0 ? make_temporal(std::move(model_path)) : std::move(model_path);
    auto result = fs_proxy.open_write(path, file_size);
    if (result.has_error()) {
        return result.assume_error();
    }

    auto &file = result.assume_value();
    return file_t(std::move(file), std::move(path), std::move(model_path), file_size);
}

auto file_t::open_read(const bfs::path &path) noexcept -> outcome::result<file_t> {
    auto file = utils::fstream_t(path, utils::fstream_t::binary | utils::fstream_t::in);
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }
    return file_t(std::move(file), path);
}

file_t::file_t() noexcept {};

file_t::file_t(utils::fstream_t backend_, bfs::path path_, bfs::path model_path_, std::uint64_t file_size_) noexcept
    : backend{new utils::fstream_t(std::move(backend_))}, path{std::move(path_)}, file_size{file_size_} {
    model_path = std::move(model_path_);
    model_path.make_preferred();
    path.make_preferred();
    path_str = narrow(model_path.generic_wstring());
}

file_t::file_t(utils::fstream_t backend_, bfs::path path_) noexcept
    : backend{new utils::fstream_t(std::move(backend_))}, path{std::move(path_)}, file_size{0} {
    path.make_preferred();
    path_str = boost::nowide::narrow(path.generic_wstring());
}

file_t::file_t(file_t &&other) noexcept : backend{nullptr} { *this = std::move(other); }

file_t &file_t::operator=(file_t &&other) noexcept {
    std::swap(backend, other.backend);
    std::swap(path, other.path);
    std::swap(path_str, other.path_str);
    std::swap(file_size, other.file_size);
    return *this;
}

file_t::~file_t() {
    if (backend && file_size) {
        auto log = utils::get_logger("fs.file");
        log->warn("closing file via d-tor '{}'", path_str);
        auto result = close(nullptr, 0);
        if (!result) {
            auto &ec = result.assume_error();
            log->warn("error closing file via d-tor '{}': {}", path_str, ec.message());
        }
    }
}

std::string_view file_t::get_path_view() const noexcept { return path_str; }

const bfs::path &file_t::get_path() const noexcept { return path; }

auto file_t::close(fs_proxy_t *fs_proxy, int64_t modification_s, const bfs::path &local_name) noexcept
    -> outcome::result<void> {
    assert(backend && file_size && "close has sense for r/w mode");
    backend.reset();

    auto rename = !local_name.empty();
    sys::error_code ec;
    auto orig_path = !rename ? &model_path : &local_name;
    if (rename) {
        if (fs_proxy) {
            ec = fs_proxy->rename(path, *orig_path);
        } else {
            bfs::rename(path, *orig_path, ec);
        }
        if (ec) {
            return ec;
        }
    } else if (*orig_path != path) {
        orig_path = &path;
    }

    if (modification_s) {
        if (fs_proxy) {
            ec = fs_proxy->last_write_time(*orig_path, modification_s);
        } else {
            auto modified = from_unix(modification_s);
            bfs::last_write_time(*orig_path, modified, ec);
        }
        if (ec) {
            return ec;
        }
    }

    return outcome::success();
}

bool file_t::has_backend() const noexcept { return backend.get(); }

auto file_t::remove(fs_proxy_t &fs_proxy) noexcept -> outcome::result<void> {
    backend.reset();

    return fs_proxy.remove(path);
}

auto file_t::read(std::uint64_t offset, std::uint64_t size) const noexcept -> outcome::result<utils::bytes_t> {
    if (backend->tellg() != offset) {
        if (!backend->seekp((long)offset, std::ios_base::beg)) {
            return sys::errc::make_error_code(sys::errc::io_error);
        }
    }

    utils::bytes_t r;
    r.resize(size);
    if (!backend->read(reinterpret_cast<char *>(r.data()), size)) {
        return sys::errc::make_error_code(sys::errc::io_error);
    }
    if (backend->gcount() != size) {
        return sys::errc::make_error_code(sys::errc::io_error);
    }

    return r;
}

auto file_t::write(fs_proxy_t &fs_proxy, uint64_t offset, utils::bytes_view_t data) noexcept -> outcome::result<void> {
    assert(offset + data.size() <= file_size);
    if (auto pos = backend->tellp(); pos != offset) {
        if (pos == -1) {
            return sys::errc::make_error_code(sys::errc::io_error);
        } else if (!backend->seekp((long)offset, std::ios_base::beg)) {
            return sys::errc::make_error_code(sys::errc::io_error);
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
