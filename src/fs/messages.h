// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "scan_task.h"
#include "file.h"
#include "chunk_iterator.h"
#include "new_chunk_iterator.h"

namespace syncspirit::fs {

namespace r = rotor;

namespace payload {

struct scan_folder_t {
    std::string folder_id;
};

struct scan_progress_t {
    scan_task_ptr_t task;
};

struct scan_completed_t {
    std::string folder_id;
};

using rehash_needed_t = chunk_iterator_t;
using hash_anew_t = new_chunk_iterator_t;

} // namespace payload

namespace message {

using scan_folder_t = r::message_t<payload::scan_folder_t>;
using scan_completed_t = r::message_t<payload::scan_completed_t>;
using scan_progress_t = r::message_t<payload::scan_progress_t>;
using rehash_needed_t = r::message_t<payload::rehash_needed_t>;
using hash_anew_t = r::message_t<payload::hash_anew_t>;

} // namespace message

} // namespace syncspirit::fs
