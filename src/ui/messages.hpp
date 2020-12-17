#pragma once

#include "../net/messages.h"

namespace syncspirit::ui {

namespace r = rotor;

namespace payload {

struct discovery_notification_t {
    using net_message_ptr_t = r::intrusive_ptr_t<net::message::discovery_notify_t>;

    net_message_ptr_t net_message;
};

} // namespace payload

namespace message {

using discovery_notify_t = r::message_t<payload::discovery_notification_t>;

}

} // namespace syncspirit::ui
