#include "test_supervisor.h"

namespace to {
struct queue{};
}

template <> inline auto &rotor::supervisor_t::access<to::queue>() noexcept { return queue; }


using namespace syncspirit::test;

void supervisor_t::do_start_timer(const r::pt::time_duration &interval, r::timer_handler_base_t &handler) noexcept {
    timers.emplace_back(&handler);
}

void supervisor_t::do_cancel_timer(r::request_id_t timer_id) noexcept {
    auto it = timers.begin();
    while (it != timers.end()) {
        auto& handler = *it;
        if (handler->request_id == timer_id) {
            auto& actor_ptr = handler->owner;
            on_timer_trigger(timer_id, true);
            timers.erase(it);
            return;
        } else {
            ++it;
        }
    }
    assert(0 && "should not happen");
}

void supervisor_t::start() noexcept {}
void supervisor_t::shutdown() noexcept { do_shutdown(); }

void supervisor_t::enqueue(r::message_ptr_t message) noexcept {
    locality_leader->access<to::queue>().emplace_back(std::move(message));
}
