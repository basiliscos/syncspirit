#pragma once
#include "base.h"

namespace syncspirit::transport {

struct stream_interface_t {
    virtual asio::ip::address local_address(sys::error_code &ec) noexcept = 0;
    virtual void async_connect(const resolved_hosts_t &hosts, connect_fn_t &on_connect,
                               error_fn_t &on_error) noexcept = 0;
    virtual void async_handshake(handshake_fn_t &on_handshake, error_fn_t &on_error) noexcept = 0;
    virtual void async_send(asio::const_buffer buff, io_fn_t &on_write, error_fn_t &on_error) noexcept = 0;
    virtual void async_recv(asio::mutable_buffer buff, io_fn_t &on_read, error_fn_t &on_error) noexcept = 0;
    virtual void cancel() noexcept = 0;
};

struct stream_base_t : model::arc_base_t<stream_base_t>, stream_interface_t {
    virtual ~stream_base_t();
};

using stream_sp_t = model::intrusive_ptr_t<stream_base_t>;

stream_sp_t initiate_stream(transport_config_t &config) noexcept;

} // namespace syncspirit::transport
