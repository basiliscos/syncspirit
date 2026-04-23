// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include "config/main.h"
#include "model/messages.h"
#include "model_actor.hpp"
#include "model/diff/cluster_visitor.h"
#include "model/misc/sequencer.h"
#include <boost/asio.hpp>
#include <rotor/asio.hpp>

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = r::asio;
namespace bfs = std::filesystem;
namespace outcome = boost::outcome_v2;

struct SYNCSPIRIT_API cluster_supervisor_t final : public model_actor_t<ra::supervisor_asio_t>,
                                                   private model::diff::cluster_visitor_t {
    using parent_t = model_actor_t<ra::supervisor_asio_t>;
    struct config_t : parent_t::config_t {
        using base_t = model_actor_t<ra::supervisor_asio_t>::config_t;
        using base_t::base_t;
        config::main_t config;
        model::sequencer_ptr_t sequencer;
    };

    template <typename Actor> struct config_builder_t : parent_t::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using base_t = parent_t::template config_builder_t<Actor>;
        using base_t::base_t;

        builder_t &&config(const config::main_t &value) && noexcept {
            base_t::config.config = value;
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }

        builder_t &&sequencer(model::sequencer_ptr_t value) && noexcept {
            base_t::config.sequencer = std::move(value);
            return std::move(*static_cast<typename base_t::builder_t *>(this));
        }
    };

    explicit cluster_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_child_shutdown(actor_base_t *actor) noexcept override;
    void on_start() noexcept override;
    void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &) noexcept override;

  private:
    outcome::result<void> operator()(const model::diff::contact::peer_state_t &, void *) noexcept override;

    config::main_t config;
    model::sequencer_ptr_t sequencer;
};

} // namespace net
} // namespace syncspirit
