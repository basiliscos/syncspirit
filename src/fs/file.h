// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <memory>
#include <boost/outcome.hpp>
#include "model/misc/arc.hpp"
#include "utils/io.h"
#include "utils/bytes.h"
#include "utils/path.h"
#include "syncspirit-export.h"

namespace syncspirit::fs {

namespace outcome = boost::outcome_v2;

namespace details {

struct chunk_t {
    utils::bytes_t data;
    size_t block_index;
};
} // namespace details

struct fs_proxy_t;

struct SYNCSPIRIT_API file_t : model::arc_base_t<file_t> {
    file_t() noexcept;
    file_t(file_t &) = delete;
    file_t(file_t &&) noexcept;
    ~file_t();

    file_t &operator=(file_t &&) noexcept;

    bool has_backend() const noexcept;
    const utils::path_t &get_path() const noexcept;

    outcome::result<void> finalize(fs_proxy_t *fs_proxy, std::int64_t modification_s,
                                   const utils::poly_path_view_t &local_name) noexcept;
    outcome::result<void> remove(fs_proxy_t &fs_proxy, const utils::allocator_t &) noexcept;
    outcome::result<void> write(fs_proxy_t &fs_proxy, std::uint64_t offset, utils::bytes_view_t data) noexcept;
    outcome::result<void> copy(fs_proxy_t &fs_proxy, std::uint64_t my_offset, const file_t &from,
                               std::uint64_t source_offset, std::uint64_t size) noexcept;
    outcome::result<utils::bytes_t> read(std::uint64_t offset, std::uint64_t size) const noexcept;

    static outcome::result<file_t> open_write(fs_proxy_t &fs_proxy, const utils::poly_path_view_t &path,
                                              std::uint64_t file_size) noexcept;
    static outcome::result<file_t> open_read(const utils::poly_path_view_t &path) noexcept;

  private:
    using backend_ptr_t = std::unique_ptr<utils::io_stream_t>;
    file_t(utils::io_stream_t backend, utils::path_t path, std::uint64_t file_size) noexcept;
    file_t(utils::io_stream_t backend, utils::path_t path) noexcept;

    backend_ptr_t backend;
    utils::path_t path;
    std::uint64_t file_size;
};

using file_ptr_t = model::intrusive_ptr_t<file_t>;

} // namespace syncspirit::fs
