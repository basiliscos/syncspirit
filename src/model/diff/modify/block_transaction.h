// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "../block_diff.h"

namespace syncspirit::model::diff::modify {

struct SYNCSPIRIT_API block_transaction_t : block_diff_t {
    using parent_t = block_diff_t;

    block_transaction_t(const file_info_t &file, const folder_info_t &fi, std::uint32_t block_index);

    cluster_diff_ptr_t ack() const;
    cluster_diff_ptr_t rej() const;
};

} // namespace syncspirit::model::diff::modify
