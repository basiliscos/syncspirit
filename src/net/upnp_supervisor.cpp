#include "upnp_supervisor.h"
#include "http_actor.h"
#include "ssdp_actor.h"

using namespace syncspirit::net;

upnp_supervisor_t::upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx,
                                     const ra::supervisor_config_t &sup_cfg, const config::upnp_config_t &cfg_)
    : ra::supervisor_asio_t(sup, ctx, sup_cfg), cfg{cfg_}, ssdp_errors{0} {}

void upnp_supervisor_t::on_shutdown(r::message_t<r::payload::shutdown_request_t> &msg) noexcept {
    if (msg.payload.actor_address == address) {
        http_addr.reset();
        ssdp_addr.reset();
    }
    ra::supervisor_asio_t::on_shutdown(msg);
}

upnp_supervisor_t::~upnp_supervisor_t() { spdlog::trace("upnp_supervisor_t:~upnp_supervisor_t"); }

void upnp_supervisor_t::launch_ssdp() noexcept {
    spdlog::trace("upnp_supervisor_t::launch_ssdp");
    ssdp_addr = create_actor<ssdp_actor_t>(cfg.max_wait)->get_address();
    ssdp_failures = 0;
}

void upnp_supervisor_t::on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept {
    if (msg.payload.actor_address == address) {
        subscribe(&upnp_supervisor_t::on_ssdp);
        subscribe(&upnp_supervisor_t::on_ssdp_failure);
    }
    ra::supervisor_asio_t::on_initialize(msg);
}

void upnp_supervisor_t::on_start(r::message_t<r::payload::start_actor_t> &msg) noexcept {
    spdlog::trace("upnp_supervisor_t::on_start");
    launch_ssdp();
    http_addr = create_actor<http_actor_t>()->get_address();
    ra::supervisor_asio_t::on_start(msg);
}

void upnp_supervisor_t::on_shutdown_confirm(r::message_t<r::payload::shutdown_confirmation_t> &msg) noexcept {
    ra::supervisor_asio_t::on_shutdown_confirm(msg);
    auto &target = msg.payload.actor_address;
    bool self_shutdown = false;

    if (target.get() == ssdp_addr.get() && !igd_url && (ssdp_failures == 0)) {
        ssdp_addr.reset();
        ++ssdp_errors;
        if (ssdp_errors < MAX_SSDP_ERRORS - 1) {
            launch_ssdp();
        } else {
            self_shutdown = true;
        }
    }
    if (target.get() == http_addr.get()) {
        self_shutdown = true;
    }
    if (self_shutdown && state == r::state_t::OPERATIONAL) {
        do_shutdown();
    }
}

void upnp_supervisor_t::on_ssdp(r::message_t<ssdp_result_t> &msg) noexcept {
    auto &result = msg.payload;
    spdlog::debug("upnp_supervisor_t::on_ssdp IGD location = {}", result.location.full);
    igd_url = result.location;
    // no longer need of ssdp
    send<r::payload::shutdown_request_t>(address, ssdp_addr);
}

void upnp_supervisor_t::on_ssdp_failure(r::message_t<ssdp_failure_t> &) noexcept {
    spdlog::trace("upnp_supervisor_t::on_ssdp_failure ({0})", ssdp_failures);
    if (ssdp_failures < MAX_SSDP_FAILURES - 1) {
        ++ssdp_failures;
        send<try_again_request_t>(ssdp_addr);
    } else {
        send<r::payload::shutdown_request_t>(address, ssdp_addr);
    }
}
