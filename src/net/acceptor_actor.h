#pragma once

#include "../configuration.h"
#include "../utils/upnp_support.h"
#include "messages.h"
#include <boost/asio.hpp>
#include <rotor/asio/supervisor_asio.h>

namespace syncspirit {
namespace net {

struct acceptor_actor_t : public r::actor_base_t {

};

} // namespace net
} // namespace syncspirit
