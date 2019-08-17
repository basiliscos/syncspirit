#pragma once

#include "messages.h"
#include "../configuration.h"
#include "rotor/asio/supervisor_asio.h"
#include <boost/optional.hpp>

namespace syncspirit {
namespace net {

namespace ra = rotor::asio;
namespace r = rotor;

class upnp_supervisor_t : public ra::supervisor_asio_t {
  public:
    upnp_supervisor_t(ra::supervisor_asio_t *sup, ra::system_context_ptr_t ctx, const ra::supervisor_config_t &sup_cfg,
                      const config::upnp_config_t &cfg);
    virtual ~upnp_supervisor_t();

    virtual void on_initialize(r::message_t<r::payload::initialize_actor_t> &msg) noexcept override;
    virtual void on_start(r::message_t<r::payload::start_actor_t> &) noexcept override;
    virtual void on_shutdown(r::message_t<r::payload::shutdown_request_t> &) noexcept override;
    virtual void on_shutdown_confirm(r::message_t<r::payload::shutdown_confirmation_t> &) noexcept override;
    void on_ssdp(r::message_t<ssdp_result_t> &) noexcept;
    void on_ssdp_failure(r::message_t<ssdp_failure_t> &) noexcept;

  private:
    using igd_url_option_t = boost::optional<utils::URI>;
    const static constexpr std::uint32_t MAX_SSDP_ERRORS = 3;
    const static constexpr std::uint32_t MAX_SSDP_FAILURES = 5;

    void launch_ssdp() noexcept;

    r::address_ptr_t http_addr;
    r::address_ptr_t ssdp_addr;
    config::upnp_config_t cfg;
    std::uint32_t ssdp_errors;
    std::uint32_t ssdp_failures;
    igd_url_option_t igd_url;
};

} // namespace net
} // namespace syncspirit
