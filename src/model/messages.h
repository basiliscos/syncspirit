// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

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

struct apply_context_t;

struct model_interrupt_t {
    model_interrupt_t(r::message_base_t *source) noexcept;
    model_interrupt_t(const model_interrupt_t &, diff::cluster_diff_t *diff = nullptr) noexcept;
    // model_interrupt_t(model_interrupt_t&&) noexcept;
    model_interrupt_t(apply_context_t &&) noexcept;

    r::message_ptr_t original;
    r::message_base_t *source = nullptr;
    std::size_t total_blocks = 0;
    std::size_t total_files = 0;
    std::size_t loaded_blocks = 0;
    std::size_t loaded_files = 0;

    diff::cluster_diff_t *diff = nullptr;
};

struct SYNCSPIRIT_API apply_context_t : model_interrupt_t {
    using parent_t = model_interrupt_t;
    apply_context_t(r::message_base_t *source, const void *message_payload) noexcept;
    apply_context_t(model_interrupt_t &) noexcept;

    const void *message_payload = nullptr;
    void *custom_payload = nullptr;
};

struct model_subscription_t {
    using fn_t = void (*)(const model::diff::cluster_diff_t &, apply_context_t &, void *);

    fn_t fn;
    void *custom;

    bool operator==(const model_subscription_t &) const noexcept = default;
};

struct model_unsubscription_t : model_subscription_t {};

struct thread_up_t {};
struct thread_ready_t {
    using thread_id_t = std::thread::id;
    model::cluster_ptr_t cluster;
    thread_id_t thread_id;
};
struct db_loaded_t {};
struct app_ready_t {};

struct local_up_t {};
struct local_ready_t {};

struct sevice_lock_t {
    std::string_view service;
};

struct sevice_unlock_t {
    std::string_view service;
};

} // namespace payload

namespace message {

using model_update_t = r::message_t<payload::model_update_t>;
using model_update_t = r::message_t<payload::model_update_t>;
using thread_up_t = r::message_t<payload::thread_up_t>;
using thread_ready_t = r::message_t<payload::thread_ready_t>;
using local_up_t = r::message_t<payload::local_up_t>;
using local_ready_t = r::message_t<payload::local_ready_t>;
using app_ready_t = r::message_t<payload::app_ready_t>;
using db_loaded_t = r::message_t<payload::db_loaded_t>;
using service_lock_t = r::message_t<payload::sevice_lock_t>;
using service_unlock_t = r::message_t<payload::sevice_unlock_t>;

using model_request_t = r::request_traits_t<payload::model_request_t>::request::message_t;
using model_response_t = r::request_traits_t<payload::model_request_t>::response::message_t;
using model_interrupt_t = r::message_t<payload::model_interrupt_t>;

using model_subscription_t = r::message_t<payload::model_subscription_t>;
using model_unsubscription_t = r::message_t<payload::model_unsubscription_t>;

} // namespace message

} // namespace syncspirit::model
