// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <rotor/request.hpp>
#include <boost/system/errc.hpp>
#include <thread>
#include "cluster.h"
#include "diff/cluster_diff.h"

namespace syncspirit::model {

namespace r = rotor;

namespace payload {

struct model_response_t {
    model::cluster_ptr_t cluster;
};

struct model_request_t {
    using response_t = model_response_t;
};

struct model_update_t {
    model::diff::cluster_diff_ptr_t diff;
    const void *custom;
};

struct model_interrupt_t {
    std::size_t total_blocks = 0;
    std::size_t total_files = 0;
    std::size_t loaded_blocks = 0;
    std::size_t loaded_files = 0;
    model::diff::cluster_diff_ptr_t diff;
};

struct thread_up_t {};
struct thread_ready_t {
    using thread_id_t = std::thread::id;
    model::cluster_ptr_t cluster;
    thread_id_t thread_id;
};
struct db_loaded_t {};
struct app_ready_t {};

} // namespace payload

namespace message {

using model_update_t = r::message_t<payload::model_update_t>;
using thread_up_t = r::message_t<payload::thread_up_t>;
using thread_ready_t = r::message_t<payload::thread_ready_t>;
using app_ready_t = r::message_t<payload::app_ready_t>;
using db_loaded_t = r::message_t<payload::db_loaded_t>;

using model_request_t = r::request_traits_t<payload::model_request_t>::request::message_t;
using model_response_t = r::request_traits_t<payload::model_request_t>::response::message_t;
using model_interrupt_t = r::message_t<payload::model_interrupt_t>;

} // namespace message

} // namespace syncspirit::model
