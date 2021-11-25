#include "peer_state.h"
#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::peer;


auto peer_state_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    assert(peer);
    peer->mark_online(online);
    return outcome::success();
}

auto peer_state_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    return visitor(*this);
}
