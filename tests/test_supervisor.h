#pragma once

#include "rotor/supervisor.h"

namespace syncspirit::test {

namespace r = rotor;

struct supervisor_t final: r::supervisor_t {
    using timers_t = std::list<r::timer_handler_base_t*>;
    using r::supervisor_t::supervisor_t;

    void start() noexcept override;
    void shutdown() noexcept override;
    void enqueue(r::message_ptr_t message) noexcept override;

    void do_start_timer(const r::pt::time_duration &interval, r::timer_handler_base_t &handler) noexcept override;
    void do_cancel_timer(r::request_id_t timer_id) noexcept override;

    timers_t timers;
};

};
