#include "peer_state.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::peer;

peer_state_t::peer_state_t(cluster_t &cluster, std::string_view peer_id_, const r::address_ptr_t &peer_addr_,
                           bool online_, std::string cert_name_, tcp::endpoint endpoint_,
                           std::string_view client_name_) noexcept
    : peer_id{peer_id_}, peer_addr{peer_addr_}, cert_name{cert_name_}, endpoint{endpoint_},
      client_name{client_name_}, online{online_} {
    known = (bool)cluster.get_devices().by_sha256(peer_id);
}

auto peer_state_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    if (known) {
        auto peer = cluster.get_devices().by_sha256(peer_id);
        peer->mark_online(online);
    }
    return outcome::success();
}

auto peer_state_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> { return visitor(*this); }