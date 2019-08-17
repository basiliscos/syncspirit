#include "upnp_supervisor.h"
#include "http_actor.h"
#include "ssdp_actor.h"

using namespace syncspirit::net;

upnp_supervisor_t::upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx,
                                     const ra::supervisor_config_t &sup_cfg, const config::upnp_config_t &cfg_)
    : ra::supervisor_asio_t(sup, ctx, sup_cfg), cfg{cfg_} {}

void upnp_supervisor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    http_addr.reset();
    ra::supervisor_asio_t::on_shutdown(msg);
}

void upnp_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_start");
    ra::supervisor_asio_t::on_start(msg);
    http_addr = create_actor<http_actor_t>()->get_address();
}
