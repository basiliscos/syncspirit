// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "proto/bep_support.h"
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

struct block_request_t {
    proto::message::Request remote_request;
    r::address_ptr_t reply_to;
    r::message_ptr_t custom;
};

struct block_response_t {
    proto::message::Request remote_request;
    r::message_ptr_t custom;
    sys::error_code ec;
    std::string data;
};

} // namespace payload

namespace message {

using scan_folder_t = r::message_t<payload::scan_folder_t>;
using scan_completed_t = r::message_t<payload::scan_completed_t>;
using scan_progress_t = r::message_t<payload::scan_progress_t>;
using rehash_needed_t = r::message_t<payload::rehash_needed_t>;
using hash_anew_t = r::message_t<payload::hash_anew_t>;
using block_request_t = r::message_t<payload::block_request_t>;
using block_response_t = r::message_t<payload::block_response_t>;

} // namespace message

} // namespace syncspirit::fs
