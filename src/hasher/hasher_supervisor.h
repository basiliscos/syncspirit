// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "config/main.h"
#include "utils/log.h"
#include "syncspirit-export.h"

#include <rotor/thread.hpp>

namespace syncspirit {
namespace hasher {

namespace r = rotor;
namespace rth = rotor::thread;

struct hasher_supervisor_config_t : r::supervisor_config_t {
    uint32_t index;
};

template <typename Supervisor> struct hasher_supervisor_config_builder_t : r::supervisor_config_builder_t<Supervisor> {
    using builder_t = typename Supervisor::template config_builder_t<Supervisor>;
    using parent_t = r::supervisor_config_builder_t<Supervisor>;
    using parent_t::parent_t;

    builder_t &&index(uint32_t value) && noexcept {
        parent_t::config.index = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct SYNCSPIRIT_API hasher_supervisor_t : rth::supervisor_thread_t {
    using parent_t = rth::supervisor_thread_t;
    using config_t = hasher_supervisor_config_t;
    template <typename Supervisor> using config_builder_t = hasher_supervisor_config_builder_t<Supervisor>;

    hasher_supervisor_t(config_t &config);

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void on_start() noexcept override;

  private:
    void launch() noexcept;

    uint32_t index;
    utils::logger_t log;
    r::address_ptr_t coordinator;
};

} // namespace hasher
} // namespace syncspirit
