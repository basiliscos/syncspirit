#pragma once

#include "messages.h"
#include "../configuration.h"
#include "rotor/asio/supervisor_asio.h"
#include <boost/optional.hpp>

namespace syncspirit {
namespace net {

class upnp_supervisor_t : public ra::supervisor_asio_t {
  public:
    struct runtime_config_t {
        r::address_ptr_t acceptor_addr;
        r::address_ptr_t peers_addr;
    };

    upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx, const ra::supervisor_config_t &sup_cfg,
                      const config::upnp_config_t &cfg, const runtime_config_t &runtime_cfg);
    virtual ~upnp_supervisor_t();

    virtual void on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept override;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message_t<r::payload::shutdown_request_t> &) noexcept override;
    virtual void on_shutdown_confirm(r::message_t<r::payload::shutdown_confirmation_t> &) noexcept override;
    virtual void on_igd_description(r::message_t<response_t> &) noexcept;
    virtual void on_external_ip(r::message_t<response_t> &) noexcept;
    virtual void on_mapping_ip(r::message_t<response_t> &) noexcept;
    virtual void on_listen_failure(r::message_t<listen_failure_t> &) noexcept;
    virtual void on_listen_success(r::message_t<listen_response_t> &) noexcept;

    void on_ssdp(r::message_t<ssdp_result_t> &) noexcept;
    void on_ssdp_failure(r::message_t<ssdp_failure_t> &) noexcept;

  private:
    using url_option_t = boost::optional<utils::URI>;
    const static constexpr std::uint32_t MAX_SSDP_ERRORS = 3;
    const static constexpr std::uint32_t MAX_SSDP_FAILURES = 5;

    void launch_ssdp() noexcept;

    r::address_ptr_t http_addr;
    r::address_ptr_t ssdp_addr;
    r::address_ptr_t acceptor_addr;
    r::address_ptr_t peers_addr;
    r::address_ptr_t addr_description; /* for routing */
    r::address_ptr_t addr_external_ip; /* for routing */
    r::address_ptr_t addr_mapping;     /* for routing */
    config::upnp_config_t cfg;
    std::uint32_t ssdp_errors;
    std::uint32_t ssdp_failures;
    url_option_t igd_url;
    url_option_t igd_control_url;
    request_t::rx_buff_ptr_t rx_buff;
    asio::ip::address external_addr;
};

} // namespace net
} // namespace syncspirit
