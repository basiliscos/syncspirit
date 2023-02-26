// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "rotor/supervisor.h"
#include "model/messages.h"
#include "utils/log.h"

namespace syncspirit::test {

namespace r = rotor;

struct supervisor_t final : r::supervisor_t {
    using timers_t = std::list<r::timer_handler_base_t *>;
    using parent_t = r::supervisor_t;
    using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

    supervisor_t(r::supervisor_config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void start() noexcept override;
    void shutdown() noexcept override;
    void enqueue(r::message_ptr_t message) noexcept override;

    void on_model_update(model::message::model_update_t &) noexcept;
    void on_block_update(model::message::block_update_t &) noexcept;
    void on_contact_update(model::message::contact_update_t &) noexcept;
    void do_start_timer(const r::pt::time_duration &interval, r::timer_handler_base_t &handler) noexcept override;
    void do_invoke_timer(r::request_id_t timer_id) noexcept;
    void do_cancel_timer(r::request_id_t timer_id) noexcept override;

    utils::logger_t log;
    model::cluster_ptr_t cluster;
    configure_callback_t configure_callback;
    timers_t timers;
};

}; // namespace syncspirit::test
