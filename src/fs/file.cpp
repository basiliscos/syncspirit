// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file.h"
#include "utils.h"
#include "utils/log.h"
#include <errno.h>
#include <cassert>

using namespace syncspirit::fs;

auto file_t::open_write(model::file_info_ptr_t model) noexcept -> outcome::result<file_t> {
    auto tmp = model->get_size() > 0;
    auto path = tmp ? make_temporal(model->get_path()) : model->get_path();
    auto path_str = path.string();

    auto mode = "r+b";
    auto ec = sys::error_code{};
    bool need_resize = true;
    auto exptected_size = (uint64_t)model->get_size();
    if (bfs::exists(path, ec)) {
        auto file_size = bfs::file_size(path, ec);
        if (ec) {
            return ec;
        }
        if (file_size == exptected_size) {
            need_resize = false;
        }
        if (file_size != exptected_size) {
        }
    } else {
        auto file = fopen(path_str.c_str(), "w");
        if (!file) {
            return sys::error_code{errno, sys::system_category()};
        }
        if (fclose(file)) {
            return sys::error_code{errno, sys::system_category()};
        }
    }

    if (need_resize) {
        bfs::resize_file(path, exptected_size, ec);
        if (ec) {
            return ec;
        }
    }

    auto file = fopen(path_str.c_str(), mode);
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }
    /*    auto r = fseek(file, 0L, SEEK_SET);
        if (r != 0) {
            return sys::error_code{errno, sys::system_category()};
        } */
    rewind(file);
    return file_t(file, std::move(model), std::move(path), tmp);
}

auto file_t::open_read(const bfs::path &path) noexcept -> outcome::result<file_t> {
    auto path_str = path.string();
    auto file = fopen(path_str.c_str(), "rb");
    if (!file) {
        return sys::error_code{errno, sys::system_category()};
    }
    return file_t(file, path);
}

file_t::file_t() noexcept : backend{nullptr} {}

file_t::file_t(FILE *backend_, model::file_info_ptr_t model_, bfs::path path_, bool temporal_) noexcept
    : backend{backend_}, model{std::move(model_)}, path{std::move(path_)}, last_op{w}, temporal{temporal_} {
    auto model_path = model->get_path();
    path_str = model_path.string();
}

file_t::file_t(FILE *backend_, bfs::path path_) noexcept
    : backend{backend_}, path{std::move(path_)}, path_str{path.string()}, last_op{r}, temporal{false} {}

file_t::file_t(file_t &&other) noexcept : backend{nullptr} { *this = std::move(other); }

file_t &file_t::operator=(file_t &&other) noexcept {
    std::swap(backend, other.backend);
    std::swap(model, other.model);
    std::swap(path, other.path);
    std::swap(path_str, other.path_str);
    std::swap(last_op, other.last_op);
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
    } else if (backend) {
        fclose(backend);
    }
}

std::string_view file_t::get_path_view() const noexcept { return path_str; }

const bfs::path &file_t::get_path() const noexcept { return path; }

auto file_t::close(bool remove_temporal, const bfs::path &local_name) noexcept -> outcome::result<void> {
    assert(backend && model && "close has sense for r/w mode");
    if (fflush(backend)) {
        return sys::error_code{errno, sys::system_category()};
    }

    if (fclose(backend)) {
        return sys::error_code{errno, sys::system_category()};
    }

    backend = nullptr;

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
    if (fclose(backend)) {
        return sys::error_code{errno, sys::system_category()};
    }
    backend = nullptr;

    sys::error_code ec;
    bfs::remove(path, ec);
    return ec;
}

auto file_t::read(size_t offset, size_t size) const noexcept -> outcome::result<std::string> {
    if (pos != offset || last_op != r) {
        auto r = fseek(backend, (long)offset, SEEK_SET);
        if (r != 0) {
            return sys::error_code{errno, sys::system_category()};
        }
    }
    last_op = r;

    std::string r;
    r.resize(size);
    auto rf = fread(r.data(), 1, size, backend);
    if (rf != size) {
        auto code = feof(backend) ? ENOENT : ferror(backend);
        return sys::error_code{code, sys::system_category()};
    }

    pos = offset + size;
    return r;
}

auto file_t::write(size_t offset, std::string_view data) noexcept -> outcome::result<void> {
    assert(offset + data.size() <= (size_t)model->get_size());
    if (pos != offset || last_op != w) {
        auto r = fseek(backend, (long)offset, SEEK_SET);
        if (r != 0) {
            return sys::error_code{errno, sys::system_category()};
        }
    }
    last_op = w;

    auto rf = fwrite(data.data(), 1, data.size(), backend);
    if (rf != data.size()) {
        return sys::error_code{errno, sys::system_category()};
    }

    pos = offset + data.size();

    rf = fflush(backend);
    if (rf) {
        return sys::error_code{errno, sys::system_category()};
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
