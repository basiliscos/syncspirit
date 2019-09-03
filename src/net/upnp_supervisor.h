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

    upnp_supervisor_t(ra::supervisor_asio_t *sup, const ra::supervisor_config_asio_t &sup_cfg,
                      const config::upnp_config_t &cfg, const runtime_config_t &runtime_cfg);
    virtual ~upnp_supervisor_t();

    virtual void on_initialize(r::message::init_request_t &msg) noexcept override;
    virtual void on_start(r::message::start_trigger_t &) noexcept override;
    virtual void shutdown_finish() noexcept override;
    virtual void on_shutdown_confirm(r::message::shutdown_responce_t &) noexcept override;
    virtual void on_initialize_confirm(r::message::init_response_t &msg) noexcept;

    virtual void on_igd_description(message::http_responce_t &) noexcept;
    virtual void on_external_ip(message::http_responce_t &) noexcept;
    virtual void on_mapping_ip(message::http_responce_t &) noexcept;
    virtual void on_listen_failure(r::message_t<listen_failure_t> &) noexcept;
    virtual void on_listen_success(r::message_t<listen_response_t> &) noexcept;

    void on_ssdp_reply(message::ssdp_responce_t &) noexcept;

  private:
    using url_option_t = boost::optional<utils::URI>;
    using duration_t = pt::time_duration;
    const static constexpr std::uint32_t MAX_SSDP_ERRORS = 3;
    const static constexpr std::uint32_t MAX_SSDP_FAILURES = 5;

    void launch_ssdp() noexcept;

    template <typename F> void make_request(const r::address_ptr_t via, const utils::URI &url, F &&fn) {
        fmt::memory_buffer tx_buff;
        auto result = fn(tx_buff);
        if (!result) {
            spdlog::error("upnp_supervisor_t:: cannot make http request: {0}", result.error().message());
            return do_shutdown();
        }
        request_via<payload::http_request_t>(http_addr, via, url, std::move(tx_buff), rx_buff, cfg.rx_buff_size)
            .timeout(pt::seconds{cfg.timeout});
    }

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
    payload::http_request_t::rx_buff_ptr_t rx_buff;
    asio::ip::address external_addr;
};

} // namespace net
} // namespace syncspirit
