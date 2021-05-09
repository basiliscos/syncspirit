#pragma once

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <rotor/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <optional>

#include <fmt/fmt.h>
#include "../model/upnp.h"
#include "../model/cluster.h"
#include "../model/peer_contact.h"
#include "../model/folder.h"
#include "../transport/base.h"
#include "../proto/bep_support.h"

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

extern r::pt::time_duration default_timeout;

namespace payload {

struct address_response_t : public r::arc_base_t<address_response_t> {
    using resolve_results_t = tcp::resolver::results_type;

    explicit address_response_t(resolve_results_t results_) : results{results_} {};
    resolve_results_t results;
};

struct address_request_t : public r::arc_base_t<address_request_t> {
    using response_t = r::intrusive_ptr_t<address_response_t>;
    std::string host;
    std::string port;
    address_request_t(const std::string &host_, std::string &port_) : host{host_}, port{port_} {}
};

struct endpoint_response_t {
    tcp::endpoint local_endpoint;
};

struct endpoint_request_t {
    using response_t = endpoint_response_t;
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

    utils::URI url;
    fmt::memory_buffer data;
    rx_buff_ptr_t rx_buff;
    std::size_t rx_buff_size;
    ssl_option_t ssl_context;
    bool local_ip = false;

    template <typename URI>
    http_request_t(URI &&url_, fmt::memory_buffer &&data_, rx_buff_ptr_t rx_buff_, std::size_t rx_buff_size_,
                   bool local_ip_)
        : url{std::forward<URI>(url_)}, data{std::move(data_)}, rx_buff{rx_buff_},
          rx_buff_size{rx_buff_size_}, local_ip{local_ip_} {}

    template <typename URI>
    http_request_t(URI &&url_, fmt::memory_buffer &&data_, rx_buff_ptr_t rx_buff_, std::size_t rx_buff_size_,
                   transport::ssl_junction_t &&ssl_)
        : http_request_t(std::forward<URI>(url_), std::move(data_), rx_buff_, rx_buff_size_, {}) {
        ssl_context = ssl_option_t(std::move(ssl_));
    }
};

struct http_close_connection_t {};

struct ssdp_notification_t : r::arc_base_t<ssdp_notification_t> {
    model::discovery_result igd;
    asio::ip::address local_address;
    ssdp_notification_t(model::discovery_result &&igd_, const asio::ip::address &local_address_)
        : igd{std::move(igd_)}, local_address{local_address_} {}
};

struct port_mapping_notification_t {
    asio::ip::address external_ip;
    bool success;
};

struct announce_notification_t {
    r::address_ptr_t source;
};

struct discovery_response_t : r::arc_base_t<discovery_response_t> {
    discovery_response_t(model::peer_contact_option_t &&peer_) noexcept : peer{std::move(peer_)} {}

    model::peer_contact_option_t peer;
};

struct discovery_request_t : r::arc_base_t<discovery_request_t> {
    using response_t = r::intrusive_ptr_t<discovery_response_t>;

    discovery_request_t(const model::device_id_t &peer_) noexcept : device_id{peer_} {}
    model::device_id_t device_id;
};

struct discovery_notification_t {
    model::device_id_t device_id;
    model::peer_contact_option_t peer;
    udp::endpoint peer_endpoint;
};

struct dial_ready_notification_t {
    model::device_id_t device_id;
    utils::uri_container_t uris;
};

struct connect_response_t : r::arc_base_t<connect_response_t> {
    r::address_ptr_t peer_addr;
    model::device_id_t peer_device_id;
    proto::ClusterConfig cluster_config;

    connect_response_t(const r::address_ptr_t &peer_addr_, model::device_id_t &peer_device_id_,
                       proto::ClusterConfig &&cluster_config_) noexcept
        : peer_addr{peer_addr_}, peer_device_id{peer_device_id_}, cluster_config{cluster_config_} {}
};

struct connect_request_t : r::arc_base_t<connect_request_t> {
    using response_t = r::intrusive_ptr_t<connect_response_t>;

    struct connect_info_t {
        model::device_id_t device_id;
        utils::uri_container_t uris;
    };

    struct connected_info_t {
        tcp_socket_t sock;
        tcp::endpoint remote;
    };

    using payload_t = std::variant<connect_info_t, connected_info_t>;

    connect_request_t(const model::device_id_t &device_id_, const utils::uri_container_t &uris_) noexcept
        : payload{connect_info_t{device_id_, uris_}} {}

    connect_request_t(tcp_socket_t &&sock, const tcp::endpoint &remote) noexcept
        : payload{connected_info_t{std::move(sock), remote}} {}

    payload_t payload;
};

struct auth_response_t : r::arc_base_t<auth_response_t> {
    using cluster_config_ptr_t = std::unique_ptr<proto::ClusterConfig>;
    cluster_config_ptr_t cluster_config;

    auth_response_t(cluster_config_ptr_t &&cluster_config_) noexcept : cluster_config{std::move(cluster_config_)} {}
};

struct auth_request_t : r::arc_base_t<auth_request_t> {
    using response_t = r::intrusive_ptr_t<auth_response_t>;

    r::address_ptr_t peer_addr;
    tcp::endpoint endpoint;
    model::device_id_t peer_device_id;
    std::string cert_name;
    proto::Hello hello;

    auth_request_t(r::address_ptr_t peer_addr_, const tcp::endpoint &endpoint_,
                   const model::device_id_t &peer_device_id_, const std::string &cert_name_,
                   proto::Hello &&hello_) noexcept
        : peer_addr{peer_addr_}, endpoint{endpoint_},
          peer_device_id{peer_device_id_}, cert_name{cert_name_}, hello{std::move(hello_)} {}
};

struct connect_notify_t {
    r::address_ptr_t peer_addr;
    model::device_id_t peer_device_id;
    proto::ClusterConfig cluster_config;
};

struct disconnect_notify_t {
    model::device_id_t peer_device_id;
    r::address_ptr_t peer_addr;
};

struct connection_notify_t {
    tcp_socket_t sock;
    tcp::endpoint remote;
};

struct load_cluster_response_t {
    model::cluster_ptr_t cluster;
    model::devices_map_t devices;
    model::ignored_devices_map_t ignored_devices;
    model::ignored_folders_map_t ignored_folders;
};

struct load_cluster_request_t {
    using response_t = load_cluster_response_t;
};

struct cluster_ready_notify_t {
    r::extended_error_ptr_t ee;
};

struct start_reading_t {
    r::address_ptr_t controller;
};

struct termination_t {
    r::extended_error_ptr_t ee;
};

using forwarded_message_t = std::variant<proto::message::Index, proto::message::IndexUpdate, proto::message::Request,
                                         proto::message::Response, proto::message::DownloadProgress>;

struct add_device_t {
    model::device_ptr_t device;
};

struct update_device_t {
    model::device_ptr_t prev_device;
    model::device_ptr_t device;
};

struct remove_device_t {
    model::device_ptr_t device;
};

struct store_ignored_device_response_t {};

struct store_ignored_device_request_t {
    using response_t = store_ignored_device_response_t;
    model::ignored_device_ptr_t device;
};

struct store_device_response_t {};

struct store_device_request_t {
    using response_t = store_device_response_t;
    model::device_ptr_t device;
};

struct store_ignored_folder_response_t {};

struct store_ignored_folder_request_t {
    using response_t = store_ignored_folder_response_t;
    model::ignored_folder_ptr_t folder;
};

struct store_new_folder_response_t {
    model::folder_ptr_t folder;
};

struct store_new_folder_request_t {
    using response_t = store_new_folder_response_t;
    db::Folder folder;
    model::device_ptr_t source;
    std::uint64_t source_index;
};

struct store_folder_response_t {};

struct store_folder_request_t {
    using response_t = store_folder_response_t;
    model::folder_ptr_t folder;
};

} // end of namespace payload

namespace message {

using ssdp_notification_t = r::message_t<payload::ssdp_notification_t>;
using announce_notification_t = r::message_t<payload::announce_notification_t>;
using port_mapping_notification_t = r::message_t<payload::port_mapping_notification_t>;

using endpoint_request_t = r::request_traits_t<payload::endpoint_request_t>::request::message_t;
using endpoint_response_t = r::request_traits_t<payload::endpoint_request_t>::response::message_t;

using resolve_request_t = r::request_traits_t<payload::address_request_t>::request::message_t;
using resolve_response_t = r::request_traits_t<payload::address_request_t>::response::message_t;
using resolve_cancel_t = r::request_traits_t<payload::address_request_t>::cancel::message_t;

using http_request_t = r::request_traits_t<payload::http_request_t>::request::message_t;
using http_response_t = r::request_traits_t<payload::http_request_t>::response::message_t;
using http_cancel_t = r::request_traits_t<payload::http_request_t>::cancel::message_t;
using http_close_connection_t = r::message_t<payload::http_close_connection_t>;

using discovery_request_t = r::request_traits_t<payload::discovery_request_t>::request::message_t;
using discovery_response_t = r::request_traits_t<payload::discovery_request_t>::response::message_t;
using discovery_cancel_t = r::request_traits_t<payload::discovery_request_t>::cancel::message_t;
using discovery_notify_t = r::message_t<payload::discovery_notification_t>;

using dial_ready_notify_t = r::message_t<payload::dial_ready_notification_t>;
using connect_request_t = r::request_traits_t<payload::connect_request_t>::request::message_t;
using connect_response_t = r::request_traits_t<payload::connect_request_t>::response::message_t;
using connect_notify_t = r::message_t<payload::connect_notify_t>;
using disconnect_notify_t = r::message_t<payload::disconnect_notify_t>;
using connection_notify_t = r::message_t<payload::connection_notify_t>;

using auth_request_t = r::request_traits_t<payload::auth_request_t>::request::message_t;
using auth_response_t = r::request_traits_t<payload::auth_request_t>::response::message_t;

using store_ignored_device_request_t = r::request_traits_t<payload::store_ignored_device_request_t>::request::message_t;
using store_ignored_device_response_t =
    r::request_traits_t<payload::store_ignored_device_request_t>::response::message_t;

using store_ignored_folder_request_t = r::request_traits_t<payload::store_ignored_folder_request_t>::request::message_t;
using store_ignored_folder_response_t =
    r::request_traits_t<payload::store_ignored_folder_request_t>::response::message_t;

using store_new_folder_request_t = r::request_traits_t<payload::store_new_folder_request_t>::request::message_t;
using store_new_folder_response_t = r::request_traits_t<payload::store_new_folder_request_t>::response::message_t;

using store_device_request_t = r::request_traits_t<payload::store_device_request_t>::request::message_t;
using store_device_response_t = r::request_traits_t<payload::store_device_request_t>::response::message_t;

using store_folder_request_t = r::request_traits_t<payload::store_folder_request_t>::request::message_t;
using store_folder_response_t = r::request_traits_t<payload::store_folder_request_t>::response::message_t;

using load_cluster_request_t = r::request_traits_t<payload::load_cluster_request_t>::request::message_t;
using load_cluster_response_t = r::request_traits_t<payload::load_cluster_request_t>::response::message_t;
using cluster_ready_notify_t = r::message_t<payload::cluster_ready_notify_t>;

using start_reading_t = r::message_t<payload::start_reading_t>;
using forwarded_message_t = r::message_t<payload::forwarded_message_t>;
using termination_signal_t = r::message_t<payload::termination_t>;

using add_device_t = r::message_t<payload::add_device_t>;
using update_device_t = r::message_t<payload::update_device_t>;
using remove_device_t = r::message_t<payload::remove_device_t>;

} // end of namespace message

} // namespace net
} // namespace syncspirit
