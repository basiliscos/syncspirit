// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <filesystem>
#include <memory>
#include <boost/outcome.hpp>
#include "model/file_info.h"
#include "utils/io.h"
#include "utils/bytes.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;
namespace bfs = std::filesystem;

namespace details {

struct chunk_t {
    utils::bytes_t data;
    size_t block_index;
};
} // namespace details

struct SYNCSPIRIT_API file_t : model::arc_base_t<file_t> {
    file_t() noexcept;
    file_t(file_t &) = delete;
    file_t(file_t &&) noexcept;
    ~file_t();

    file_t &operator=(file_t &&) noexcept;

    std::string_view get_path_view() const noexcept;
    const bfs::path &get_path() const noexcept;

    outcome::result<void> close(bool remove_temporal, const bfs::path &local_name = {}) noexcept;
    outcome::result<void> remove() noexcept;
    outcome::result<void> write(size_t offset, utils::bytes_view_t data) noexcept;
    outcome::result<void> copy(size_t my_offset, const file_t &from, size_t source_offset, size_t size) noexcept;
    outcome::result<utils::bytes_t> read(size_t offset, size_t size) const noexcept;

    static outcome::result<file_t> open_write(model::file_info_ptr_t model) noexcept;
    static outcome::result<file_t> open_read(const bfs::path &path) noexcept;

  private:
    using backend_ptr_t = std::unique_ptr<utils::fstream_t>;
    file_t(utils::fstream_t backend, model::file_info_ptr_t model, bfs::path path, bool temporal) noexcept;
    file_t(utils::fstream_t backend, bfs::path path) noexcept;

    backend_ptr_t backend;
    model::file_info_ptr_t model;
    bfs::path path;
    std::string path_str;
    bool temporal{false};
};

using file_ptr_t = model::intrusive_ptr_t<file_t>;

} // namespace syncspirit::fs
