// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <filesystem>
#include <memory>
#include <boost/outcome.hpp>
#include "model/misc/arc.hpp"
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

    bool has_backend() const noexcept;
    std::string_view get_path_view() const noexcept;
    const bfs::path &get_path() const noexcept;

    outcome::result<void> close(std::int64_t modification_s, const bfs::path &local_name = {}) noexcept;
    outcome::result<void> remove() noexcept;
    outcome::result<void> write(std::uint64_t offset, utils::bytes_view_t data) noexcept;
    outcome::result<void> copy(std::uint64_t my_offset, const file_t &from, std::uint64_t source_offset,
                               std::uint64_t size) noexcept;
    outcome::result<utils::bytes_t> read(std::uint64_t offset, std::uint64_t size) const noexcept;

    static outcome::result<file_t> open_write(const bfs::path &path, std::uint64_t file_size) noexcept;
    static outcome::result<file_t> open_read(const bfs::path &path) noexcept;

  private:
    using backend_ptr_t = std::unique_ptr<utils::fstream_t>;
    file_t(utils::fstream_t backend, bfs::path path, bfs::path model_path, std::uint64_t file_size) noexcept;
    file_t(utils::fstream_t backend, bfs::path path) noexcept;

    backend_ptr_t backend;
    bfs::path path;
    bfs::path model_path;
    std::string path_str;
    std::uint64_t file_size;
};

using file_ptr_t = model::intrusive_ptr_t<file_t>;

} // namespace syncspirit::fs
