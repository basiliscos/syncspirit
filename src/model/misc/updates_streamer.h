// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2023 Ivan Baidakou

#pragma once

#include "arc.hpp"
#include "model/cluster.h"
#include "model/file_info.h"
#include "model/device.h"
#include "model/folder_info.h"
#include "model/remote_folder_info.h"
#include "syncspirit-export.h"

#include <unordered_set>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/identity.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/mem_fun.hpp>

namespace syncspirit::model {

namespace details {

inline std::string_view get_uuid(const file_info_ptr_t &item) noexcept { return item->get_uuid(); }
inline std::int64_t get_sequence(const file_info_ptr_t &item) noexcept { return item->get_sequence(); }

namespace mi = boost::multi_index;
using files_set_t = mi::multi_index_container<
    file_info_ptr_t,
    mi::indexed_by<mi::ordered_unique<mi::global_fun<const file_info_ptr_t &, std::string_view, &get_uuid>>,
                   mi::ordered_unique<mi::global_fun<const file_info_ptr_t &, std::int64_t, &get_sequence>>>>;

} // namespace details

struct SYNCSPIRIT_API updates_streamer_t {
    updates_streamer_t() noexcept = default;
    updates_streamer_t(cluster_t &, device_t &) noexcept;

    updates_streamer_t &operator=(updates_streamer_t &&) noexcept;

    operator bool() const noexcept;
    file_info_ptr_t next() noexcept;

    void on_update(file_info_t &) noexcept;

  private:
    using folders_queue_t = std::unordered_set<remote_folder_info_t_ptr_t>;

    void prepare() noexcept;

    device_ptr_t peer;
    folders_queue_t folders_queue;
    details::files_set_t files_queue;
};

} // namespace syncspirit::model
