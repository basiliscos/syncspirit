// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file.h"
#include "utils.h"
#include "utils/log.h"
#include <errno.h>
#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>
#include <boost/nowide/convert.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

using namespace syncspirit::fs;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#define SS_STAT_FN(PATH, BUFF) _wstat64((PATH).wstring().data(), (BUFF))
#define SS_STAT_BUFF struct __stat64
#else
#define SS_STAT_FN(PATH, BUFF) stat(boost::nowide::narrow((PATH).generic_wstring()).data(), (BUFF))
#define SS_STAT_BUFF struct stat
#endif

auto file_t::open_write(model::file_info_ptr_t model) noexcept -> outcome::result<file_t> {
    using mode_t = utils::fstream_t;
    auto tmp = model->get_size() > 0;
    auto path = tmp ? make_temporal(model->get_path()) : model->get_path();
    path.make_preferred();

    bool need_resize = true;
    auto expected_size = (uint64_t)model->get_size();

    SS_STAT_BUFF stat_info;
    auto r = SS_STAT_FN(path, &stat_info);
    if (r == 0) {
        need_resize = static_cast<uint64_t>(stat_info.st_size) == expected_size;
    }
    auto mode = mode_t::in | mode_t::out | mode_t::binary;
    if (need_resize) {
        if (r == -1) {
            auto file = utils::fstream_t(path, mode | mode_t::trunc);
            mode = mode & ~mode_t::trunc;
        }
        auto ec = std::error_code();
        bfs::resize_file(path, expected_size, ec);
        if (ec) {
            return ec;
        }
    }

    auto file = utils::fstream_t(path, mode);
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }

    return file_t(std::move(file), std::move(model), std::move(path), tmp);
}

auto file_t::open_read(const bfs::path &path) noexcept -> outcome::result<file_t> {
    auto file = utils::fstream_t(path, utils::fstream_t::binary | utils::fstream_t::in);
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }
    return file_t(std::move(file), path);
}

file_t::file_t() noexcept {};

file_t::file_t(utils::fstream_t backend_, model::file_info_ptr_t model_, bfs::path path_, bool temporal_) noexcept
    : backend{new utils::fstream_t(std::move(backend_))}, model{std::move(model_)}, path{std::move(path_)},
      temporal{temporal_} {
    auto model_path = model->get_path();
    model_path.make_preferred();
    path.make_preferred();
    path_str = boost::nowide::narrow(model_path.generic_wstring());
}

file_t::file_t(utils::fstream_t backend_, bfs::path path_) noexcept
    : backend{new utils::fstream_t(std::move(backend_))}, path{std::move(path_)}, temporal{false} {
    path.make_preferred();
    path_str = boost::nowide::narrow(path.generic_wstring());
}

file_t::file_t(file_t &&other) noexcept : backend{nullptr} { *this = std::move(other); }

file_t &file_t::operator=(file_t &&other) noexcept {
    std::swap(backend, other.backend);
    std::swap(model, other.model);
    std::swap(path, other.path);
    std::swap(path_str, other.path_str);
    std::swap(temporal, other.temporal);
    return *this;
}

file_t::~file_t() {
    if (backend && model) {
        auto result = close(model->is_locally_available());
        if (!result) {
            auto log = utils::get_logger("fs.file");
            auto &ec = result.assume_error();
            log->warn("(ignored) error closing file '{}' : {}", path_str, ec.message());
        }
    }
}

std::string_view file_t::get_path_view() const noexcept { return path_str; }

const bfs::path &file_t::get_path() const noexcept { return path; }

auto file_t::close(bool remove_temporal, const bfs::path &local_name) noexcept -> outcome::result<void> {
    assert(backend && model && "close has sense for r/w mode");
    backend.reset();

    sys::error_code ec;
    auto orig_path = local_name.empty() ? model->get_path() : local_name;
    if (remove_temporal) {
        assert(temporal);
        bfs::rename(path, orig_path, ec);
        if (ec) {
            return ec;
        }
    } else if (orig_path != path) {
        orig_path = path;
    }

    auto modified = from_unix(model->get_modified_s());
    bfs::last_write_time(orig_path, modified, ec);
    if (ec) {
        return ec;
    }

    return outcome::success();
}

auto file_t::remove() noexcept -> outcome::result<void> {
    backend.reset();

    sys::error_code ec;
    bfs::remove(path, ec);
    return ec;
}

auto file_t::read(size_t offset, size_t size) const noexcept -> outcome::result<utils::bytes_t> {
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

auto file_t::write(size_t offset, utils::bytes_view_t data) noexcept -> outcome::result<void> {
    assert(offset + data.size() <= (size_t)model->get_size());
    if (backend->tellp() != offset) {
        if (!backend->seekp((long)offset, std::ios_base::beg)) {
            return sys::errc::make_error_code(sys::errc::io_error);
        }
    }

    auto ptr = reinterpret_cast<const char *>(data.data());
    if (!backend->write(ptr, data.size())) {
        return sys::errc::make_error_code(sys::errc::io_error);
    }
    if (!backend->flush()) {
        return sys::errc::make_error_code(sys::errc::io_error);
    }
    return outcome::success();
}

auto file_t::copy(size_t my_offset, const file_t &from, size_t source_offset, size_t size) noexcept
    -> outcome::result<void> {
    auto in_opt = from.read(source_offset, size);
    if (!in_opt) {
        return in_opt.assume_error();
    }

    auto &in = in_opt.assume_value();
    return write(my_offset, in);
}
