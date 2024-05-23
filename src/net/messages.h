// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rotor/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <optional>

#include <fmt/core.h>
#include "model/misc/upnp.h"
#include "model/cluster.h"
#include "transport/base.h"
#include "proto/bep_support.h"

namespace syncspirit {
namespace net {

namespace r = rotor;
namespace ra = rotor::asio;
namespace asio = boost::asio;
namespace pt = boost::posix_time;
namespace http = boost::beast::http;
namespace sys = boost::system;
namespace ssl = asio::ssl;

using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using v4 = asio::ip::address_v4;
using udp_socket_t = udp::socket;
using tcp_socket_t = tcp::socket;

namespace payload {

using cluster_config_ptr_t = std::unique_ptr<proto::ClusterConfig>;

struct address_response_t : public r::arc_base_t<address_response_t> {
    using resolve_results_t = tcp::resolver::results_type;

    explicit address_response_t(resolve_results_t results_) : results{results_} {};
    resolve_results_t results;
};

struct address_request_t : public r::arc_base_t<address_request_t> {
    using response_t = r::intrusive_ptr_t<address_response_t>;
    std::string host;
    std::string port;
    address_request_t(std::string_view host_, std::string_view port_) : host{host_}, port{port_} {}
};

struct http_response_t : public r::arc_base_t<http_response_t> {
    using raw_http_response_t = http::response<http::string_body>;
    raw_http_response_t response;
    std::size_t bytes;
    std::optional<asio::ip::address> local_addr;

    http_response_t(raw_http_response_t &&response_, std::size_t bytes_,
                    std::optional<asio::ip::address> local_addr_ = {})
        : response(std::move(response_)), bytes{bytes_}, local_addr{std::move(local_addr_)} {}
};

struct http_request_t : r::arc_base_t<http_request_t> {
    using rx_buff_t = boost::beast::flat_buffer;
    using rx_buff_ptr_t = std::shared_ptr<rx_buff_t>;
    using duration_t = r::pt::time_duration;
    using ssl_option_t = transport::ssl_option_t;
    using response_t = r::intrusive_ptr_t<http_response_t>;

    utils::uri_ptr_t url;
    fmt::memory_buffer data;
    rx_buff_ptr_t rx_buff;
    std::size_t rx_buff_size;
    ssl_option_t ssl_context;
    bool local_ip = false;
    r::message_ptr_t custom;

    template <typename URI>
    http_request_t(URI &&url_, fmt::memory_buffer &&data_, rx_buff_ptr_t rx_buff_, std::size_t rx_buff_size_,
                   bool local_ip_, const r::message_ptr_t &custom_ = {})
        : url{std::forward<URI>(url_)}, data{std::move(data_)}, rx_buff{rx_buff_}, rx_buff_size{rx_buff_size_},
          local_ip{local_ip_}, custom{custom_} {}

    template <typename URI>
    http_request_t(URI &&url_, fmt::memory_buffer &&data_, rx_buff_ptr_t rx_buff_, std::size_t rx_buff_size_,
                   transport::ssl_junction_t &&ssl_, const r::message_ptr_t &custom_ = {})
        : http_request_t(std::forward<URI>(url_), std::move(data_), rx_buff_, rx_buff_size_, {}, custom_) {
        ssl_context = ssl_option_t(std::move(ssl_));
    }
};

struct http_close_connection_t {};

struct announce_notification_t {
    r::address_ptr_t source;
};

struct discovery_notification_t {
    model::device_id_t device_id;
};

struct load_cluster_response_t {
    model::diff::cluster_diff_ptr_t diff;
};

struct load_cluster_request_t {
    using response_t = load_cluster_response_t;
};

struct start_reading_t {
    r::address_ptr_t controller;
    bool start;
};

struct termination_t {
    r::extended_error_ptr_t ee;
};

using forwarded_message_t =
    std::variant<proto::message::ClusterConfig, proto::message::Index, proto::message::IndexUpdate,
                 proto::message::Request, proto::message::DownloadProgress>;

struct block_response_t {
    std::string data;
};

struct block_request_t {
    using response_t = block_response_t;
    model::file_info_ptr_t file;
    model::file_block_t block;
    block_request_t(const model::file_info_ptr_t &file, const model::file_block_t &block) noexcept;
    ~block_request_t();
};

struct connect_response_t {
    transport::stream_sp_t transport;
    tcp::endpoint remote_endpoint;
};

struct connect_request_t {
    using response_t = connect_response_t;
    model::device_id_t device_id;
    utils::uri_ptr_t uri;
    std::string_view alpn;
};

struct transfer_data_t {
    fmt::memory_buffer data;
};

struct transfer_push_t {
    uint32_t bytes;
};

struct transfer_pop_t {
    uint32_t bytes;
};

} // end of namespace payload

namespace message {

using announce_notification_t = r::message_t<payload::announce_notification_t>;

using resolve_request_t = r::request_traits_t<payload::address_request_t>::request::message_t;
using resolve_response_t = r::request_traits_t<payload::address_request_t>::response::message_t;
using resolve_cancel_t = r::request_traits_t<payload::address_request_t>::cancel::message_t;

using http_request_t = r::request_traits_t<payload::http_request_t>::request::message_t;
using http_response_t = r::request_traits_t<payload::http_request_t>::response::message_t;
using http_cancel_t = r::request_traits_t<payload::http_request_t>::cancel::message_t;
using http_close_connection_t = r::message_t<payload::http_close_connection_t>;

using discovery_notify_t = r::message_t<payload::discovery_notification_t>;

using load_cluster_request_t = r::request_traits_t<payload::load_cluster_request_t>::request::message_t;
using load_cluster_response_t = r::request_traits_t<payload::load_cluster_request_t>::response::message_t;

using start_reading_t = r::message_t<payload::start_reading_t>;
using forwarded_message_t = r::message_t<payload::forwarded_message_t>;
using termination_signal_t = r::message_t<payload::termination_t>;
using transfer_data_t = r::message_t<payload::transfer_data_t>;
using transfer_push_t = r::message_t<payload::transfer_push_t>;
using transfer_pop_t = r::message_t<payload::transfer_pop_t>;

using block_request_t = r::request_traits_t<payload::block_request_t>::request::message_t;
using block_response_t = r::request_traits_t<payload::block_request_t>::response::message_t;

using connect_request_t = r::request_traits_t<payload::connect_request_t>::request::message_t;
using connect_response_t = r::request_traits_t<payload::connect_request_t>::response::message_t;

} // end of namespace message

} // namespace net
} // namespace syncspirit
