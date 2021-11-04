#include "./update_peer.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

auto update_peer_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto& devices = cluster.get_devices();
    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }
    peer->update(item);

    return outcome::success();
}
