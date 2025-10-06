// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "rotor/supervisor.h"
#include "model/messages.h"
#include "bouncer/messages.hpp"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/local/io_failure.h"
#include "model/misc/sequencer.h"
#include "utils/log.h"
#include "syncspirit-test-export.h"

namespace syncspirit::test {

namespace r = rotor;
namespace outcome = boost::outcome_v2;

using configure_callback_t = std::function<void(r::plugin::plugin_base_t &)>;

struct supervisor_config_t : r::supervisor_config_t {
    using parent_t = r::supervisor_config_t;
    using parent_t::parent_t;
    bool auto_finish = true;
    bool auto_ack_blocks = true;
    bool make_presentation = false;
    configure_callback_t configure_callback;
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

    builder_t &&auto_ack_blocks(bool value) && noexcept {
        parent_t::config.auto_ack_blocks = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&make_presentation(bool value) && noexcept {
        parent_t::config.make_presentation = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
    builder_t &&configure_callback(configure_callback_t value) && noexcept {
        parent_t::config.configure_callback = std::move(value);
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_TEST_API supervisor_t : r::supervisor_t,
                                          model::diff::apply_controller_t,
                                          protected model::diff::cluster_visitor_t {
    using config_t = supervisor_config_t;
    template <typename Actor> using config_builder_t = supervisor_config_builder_t<Actor>;

    using timers_t = std::list<r::timer_handler_base_t *>;
    using parent_t = r::supervisor_t;
    using io_errors_t = model::diff::local::io_errors_t;

    supervisor_t(config_t &cfg);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void start() noexcept override;
    void shutdown() noexcept override;
    void enqueue(r::message_ptr_t message) noexcept override;

    virtual void on_model_update(model::message::model_update_t &) noexcept;
    virtual void on_package(bouncer::message::package_t &) noexcept;
    void on_model_sink(model::message::model_update_t &) noexcept;
    void do_start_timer(const r::pt::time_duration &interval, r::timer_handler_base_t &handler) noexcept override;
    void do_invoke_timer(r::request_id_t timer_id) noexcept;
    void do_cancel_timer(r::request_id_t timer_id) noexcept override;
    io_errors_t consume_errors() noexcept;

    outcome::result<void> operator()(const model::diff::local::io_failure_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::finish_file_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::append_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::clone_block_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    outcome::result<void> apply(const model::diff::load::commit_t &, void *) noexcept override;

    using model::diff::apply_controller_t::cluster;

    utils::logger_t log;
    model::sequencer_ptr_t sequencer;
    configure_callback_t configure_callback;
    model::diff::cluster_diff_ptr_t delayed_ack_holder;
    model::diff::cluster_diff_t *delayed_ack_current;
    timers_t timers;
    bool auto_finish;
    bool auto_ack_blocks;
    bool make_presentation;
    io_errors_t io_errors;
};

}; // namespace syncspirit::test
