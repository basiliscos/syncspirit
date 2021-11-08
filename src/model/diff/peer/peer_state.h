#pragma once

#include <rotor/address.hpp>
#include "../cluster_diff.h"
#include <boost/asio/ip/tcp.hpp>

namespace syncspirit::model::diff::peer {

namespace r = rotor;
using tcp = boost::asio::ip::tcp;

struct peer_state_t final : cluster_diff_t {

    peer_state_t(std::string_view peer_id_, const r::address_ptr_t& peer_addr_, bool online_, std::string cert_name_ = {},
                 tcp::endpoint endpoint_ = {}, std::string_view client_name_ = {}) noexcept:
        peer_id{peer_id_}, peer_addr{peer_addr_}, cert_name{cert_name_}, endpoint{endpoint_}, client_name{client_name_}, online{online_}
    {}

    outcome::result<void> apply_impl(cluster_t &) const noexcept override;
    outcome::result<void> visit(diff_visitor_t &) const noexcept override;

    std::string peer_id;
    r::address_ptr_t peer_addr;
    std::string cert_name;
    tcp::endpoint endpoint;
    std::string client_name;
    bool online;


};

}
