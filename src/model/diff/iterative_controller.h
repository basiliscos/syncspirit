// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "apply_controller.h"
#include "cluster_visitor.h"
#include "model/messages.h"
#include <list>
#include <rotor/plugin/plugin_base.h>

namespace syncspirit::model::diff {

struct SYNCSPIRIT_API iterative_controller_base_t : apply_controller_t, protected cluster_visitor_t {
    using apply_context_t = payload::model_interrupt_t;
    using model_update_ptr_t = r::intrusive_ptr_t<model::message::model_update_t>;
    using delayed_updates_t = std::list<model_update_ptr_t>;

    iterative_controller_base_t(r::actor_base_t *owner) noexcept;

  protected:
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_model_interrupt(model::message::model_interrupt_t &message) noexcept;
    void process(model::diff::cluster_diff_t &diff, apply_context_t *apply_context, const void *custom) noexcept;
    virtual void process(model::diff::cluster_diff_t &diff, const void *custom) noexcept;
    virtual outcome::result<void> visit_diff(model::diff::cluster_diff_t &diff, apply_context_t *apply_context,
                                             const void *custom) noexcept;
    virtual void commit_loading() noexcept;

    outcome::result<void> apply(const model::diff::load::interrupt_t &, model::cluster_t &, void *) noexcept override;
    outcome::result<void> apply(const model::diff::load::commit_t &, model::cluster_t &, void *) noexcept override;

    r::actor_base_t *owner;
    utils::logger_t log;

    model::cluster_ptr_t cluster;
    delayed_updates_t delayed_updates;
    r::address_ptr_t coordinator;
    r::address_ptr_t bouncer;
    bool interrupted = false;
};

template <typename T, typename Parent> struct iterative_controller_t : iterative_controller_base_t, Parent {
    using base_t = iterative_controller_base_t;

    template <typename... Args>
    iterative_controller_t(T *self, Args &&...args) noexcept : base_t(self), Parent(std::forward<Args>(args)...) {}
    void on_model_update(model::message::model_update_t &message) noexcept { base_t::on_model_update(message); }
    void on_model_interrupt(model::message::model_interrupt_t &message) noexcept {
        base_t::on_model_interrupt(message);
    }
};

} // namespace syncspirit::model::diff
