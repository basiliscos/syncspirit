#include "test_supervisor.h"
#include "net/names.h"

namespace to {
struct queue{};
}

template <> inline auto &rotor::supervisor_t::access<to::queue>() noexcept { return queue; }


using namespace syncspirit::net;
using namespace syncspirit::test;

supervisor_t::supervisor_t(r::supervisor_config_t& cfg): r::supervisor_t(cfg) {
    log = utils::get_logger("net.test_supervisor");
}


void supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::coordinator, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::coordinator, get_address());
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&supervisor_t::on_model_update);
    });
    if (configure_callback) {
        configure_callback(plugin);
    }
}


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

void supervisor_t::on_model_update(net::message::model_update_t& msg) noexcept {
    LOG_TRACE(log, "{}, updating model", identity);
    auto& diff = msg.payload.diff;
    auto r = diff->apply(*cluster);
    if (!r) {
        LOG_ERROR(log, "{}, error updating model: {}", r.assume_error().message());
        do_shutdown(make_error(r.assume_error()));
    }
}
