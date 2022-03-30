// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "messages.h"

using namespace syncspirit::fs::payload;

rehash_needed_t::rehash_needed_t(scan_task_ptr_t task_, model::file_info_ptr_t file_,
                                 model::file_info_ptr_t source_file_, file_ptr_t backend_) noexcept
    : task{std::move(task_)}, file{std::move(file_)}, source_file{std::move(source_file_)}, backend{std::move(
                                                                                                backend_)},
      last_queued_block{0}, valid_blocks{-1}, queue_size{0}, out_of_order{}, abandoned{false}, invalid{false} {
    unhashed_blocks = source_file->get_blocks().size();
}
