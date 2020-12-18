#pragma once

#include "activity.h"
#include "../ui/messages.hpp"

namespace syncspirit::console {

struct local_peer_activity_t : activity_t {
    using message_ptr_t = ui::payload::discovery_notification_t::net_message_ptr_t;

    local_peer_activity_t(tui_actor_t &actor_, activity_type_t type_,
                          ui::message::discovery_notify_t &message) noexcept;
    bool handle(const char key) noexcept override;
    void display() noexcept override;
    bool operator==(const activity_t &other) const noexcept override;

    message_ptr_t message;
};

} // namespace syncspirit::console
