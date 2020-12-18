#include "local_peer_activity.h"

#include "default_activity.h"
#include "tui_actor.h"
#include "sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;

local_peer_activity_t::local_peer_activity_t(tui_actor_t &actor_, activity_type_t type_,
                                             ui::message::discovery_notify_t &message_) noexcept
    : activity_t{actor_, type_}, message{message_.payload.net_message} {}

bool local_peer_activity_t::handle(const char key) noexcept { return false; }

void local_peer_activity_t::display() noexcept {
    auto &payload = message->payload;
    auto p = fmt::format("add device {}{}{}{} ({}) y/n? ", sink_t::bold, sink_t::white, payload.device_id.get_value(),
                         sink_t::reset, payload.peer_endpoint.address().to_string());
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool local_peer_activity_t::operator==(const activity_t &other) const noexcept {
    if (type != other.type) {
        return false;
    }
    auto o = (const local_peer_activity_t &)other;
    return message->payload.device_id == o.message->payload.device_id;
}
