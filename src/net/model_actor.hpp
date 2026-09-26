// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "model/messages.h"
#include "names.h"

namespace syncspirit::net {

namespace r = rotor;

template <typename Parent> struct model_actor_t : public Parent {
    using parent_t = Parent;
    using parent_t::parent_t;

    struct config_t : Parent::config_t {
        using parent_t = Parent::config_t;
        using parent_t::parent_t;
        model::cluster_ptr_t cluster;
    };

    template <typename Actor> struct config_builder_t : Parent::template config_builder_t<Actor> {
        using builder_t = typename Actor::template config_builder_t<Actor>;
        using parent_t = Parent::template config_builder_t<Actor>;
        using parent_t::parent_t;

        builder_t &&cluster(const model::cluster_ptr_t &value) && noexcept {
            parent_t::config.cluster = value;
            return std::move(*static_cast<typename parent_t::builder_t *>(this));
        }
    };

    static inline void model_visit_callback(const model::diff::cluster_diff_t &diff,
                                            model::payload::apply_context_t &ctx, void *custom) {
        reinterpret_cast<model_actor_t *>(custom)->visit(diff, ctx);
    }

    model_actor_t(config_t &cfg) : parent_t(cfg), cluster{cfg.cluster} {}

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
                if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                    post_configure_coordinator();
                }
            });
        });
    }

    virtual void visit(const model::diff::cluster_diff_t &, model::payload::apply_context_t &ctx) noexcept = 0;

    void on_start() noexcept override {
        LOG_DEBUG(log, "on_start");
        parent_t::on_start();
    }

    void shutdown_start() noexcept override {
        LOG_DEBUG(log, "shutdown_start");
        parent_t::template send<model::payload::model_unsubscription_t>(coordinator, model_visit_callback, this);
        parent_t::shutdown_start();
    }

  protected:
    virtual void post_configure_coordinator() noexcept {
        parent_t::template send<model::payload::model_subscription_t>(coordinator, model_visit_callback, this);
    }

    utils::logger_t log;
    r::address_ptr_t coordinator;
    model::cluster_ptr_t cluster;
};

} // namespace syncspirit::net
