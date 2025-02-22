// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>
#include "model/messages.h"

namespace syncspirit {
namespace net {

namespace r = rotor;

struct SYNCSPIRIT_API sink_actor_t : public r::actor_base_t {
    using r::actor_base_t::actor_base_t;
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    void on_model_sink(model::message::model_update_t &message) noexcept;

    utils::logger_t log;
    r::address_ptr_t coordinator;
};

} // namespace net
} // namespace syncspirit
