// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "rotor/supervisor.h"
#include "model/messages.h"
#include "model/diff/cluster_visitor.h"
#include "utils/log.h"
#include "syncspirit-test-export.h"

namespace syncspirit::test {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

struct supervisor_config_t : r::supervisor_config_t {
    using parent_t = r::supervisor_config_t;
    using parent_t::parent_t;
    bool auto_finish = true;
};

template <typename Supervisor> struct supervisor_config_builder_t : r::supervisor_config_builder_t<Supervisor> {
    /** \brief final builder class */
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;

    /** \brief parent config builder */
    using parent_t = r::supervisor_config_builder_t<Supervisor>;

    using parent_t::parent_t;

    /** \brief defines actor's startup policy */
    builder_t &&auto_finish(bool value) && noexcept {
        parent_t::config.auto_finish = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_TEST_API supervisor_t : r::supervisor_t, private model::diff::cluster_visitor_t {
    using config_t = supervisor_config_t;
    template <typename Actor> using config_builder_t = supervisor_config_builder_t<Actor>;

    using timers_t = std::list<r::timer_handler_base_t *>;
    using parent_t = r::supervisor_t;
    using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

    supervisor_t(config_t &cfg);
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

    outcome::result<void> operator()(const model::diff::modify::finish_file_t &, void *custom) noexcept override;

    utils::logger_t log;
    model::cluster_ptr_t cluster;
    configure_callback_t configure_callback;
    timers_t timers;
    bool auto_finish;
};

}; // namespace syncspirit::test
