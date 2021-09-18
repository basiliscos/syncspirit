#include "add_peer.h"
#include "../governor_actor.h"
#include "../error_code.h"

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> add_peer_t::construct(std::string_view in) noexcept {
    auto colon = in.find(":");
    if (colon == in.npos) {
        return make_error_code(error_code_t::missing_device_label);
    }

    auto label = in.substr(0, colon);

    auto opt = model::device_id_t::from_string(in.substr(colon + 1));
    if (!opt) {
        return make_error_code(error_code_t::invalid_device_id);
    }
    return std::make_unique<command::add_peer_t>(opt.value(), label);
}

bool add_peer_t::execute(governor_actor_t &actor) noexcept {
    log = actor.log;
    auto &devices = actor.devices_copy;
    bool found = false;
    for (auto it : devices) {
        auto &d = it.second;
        if (d->device_id == peer) {
            found = true;
            break;
        }
    }
    if (found) {
        log->warn("{}, device {} is already added, skipping", actor.get_identity(), peer);
        return false;
    }
    db::Device db_dev;
    db_dev.set_id(peer.get_sha256());
    db_dev.set_name(label);
    auto device = model::device_ptr_t(new model::device_t(db_dev));
    actor.cmd_add_peer(device);
    return true;
}

} // namespace syncspirit::daemon::command
