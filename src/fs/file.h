// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <cstdio>
#include <string>
#include <boost/outcome.hpp>
#include <boost/filesystem.hpp>
#include "model/file_info.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = boost::filesystem;

struct file_t : model::arc_base_t<file_t> {
    file_t() noexcept;
    file_t(file_t &) = delete;
    file_t(file_t &&) noexcept;
    ~file_t();

    file_t &operator=(file_t &&) noexcept;

    std::string_view get_path_view() const noexcept;
    const bfs::path &get_path() const noexcept;

    outcome::result<void> close(bool remove_temporal) noexcept;
    outcome::result<void> remove() noexcept;
    outcome::result<void> write(size_t offset, std::string_view data) noexcept;
    outcome::result<void> copy(size_t my_offset, const file_t &from, size_t source_offset, size_t size) noexcept;
    outcome::result<std::string> read(size_t offset, size_t size) const noexcept;

    static outcome::result<file_t> open_write(model::file_info_ptr_t model) noexcept;
    static outcome::result<file_t> open_read(const bfs::path &path) noexcept;

  private:
    file_t(FILE *backend, model::file_info_ptr_t model, bfs::path path, bool temporal) noexcept;
    file_t(FILE *backend, bfs::path path) noexcept;

    enum last_op_t { r, w };

    FILE *backend;
    model::file_info_ptr_t model;
    bfs::path path;
    std::string path_str;
    mutable size_t pos = 0;
    mutable last_op_t last_op;
    bool temporal;
};

using file_ptr_t = model::intrusive_ptr_t<file_t>;

} // namespace syncspirit::fs
