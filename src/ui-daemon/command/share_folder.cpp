#include "share_folder.h"
#include "pair_iterator.h"
#include "../governor_actor.h"
#include "../error_code.h"

namespace syncspirit::daemon::command {

outcome::result<command_ptr_t> share_folder_t::construct(std::string_view in) noexcept {
    std::string folder, peer;
    auto it = pair_iterator_t(in);
    while (true) {
        auto r = it.next();
        if (r) {
            auto &v = r.value();
            if (v.first == "folder") {
                folder = v.second;
            } else if (v.first == "device") {
                peer = v.second;
            }
        } else {
            break;
        }
    }
    if (folder.empty()) {
        return make_error_code(error_code_t::missing_folder);
    }
    if (peer.empty()) {
        return make_error_code(error_code_t::missing_device);
    }

    return command_ptr_t(new share_folder_t(std::move(folder), std::move(peer)));
}

bool share_folder_t::execute(governor_actor_t &actor) noexcept {
    log = actor.log;
    model::folder_ptr_t folder;
    for (auto it : actor.cluster_copy->get_folders()) {
        auto f = it.second;
        if ((f->id() == this->folder)) {
            folder = f;
            break;
        }
        if (f->label() == this->folder) {
            folder = f;
            break;
        }
    }
    if (!folder) {
        log->warn("{}, folder {} not found", actor.get_identity(), this->folder);
        return false;
    }

    model::device_ptr_t device;
    for (auto it : actor.devices_copy) {
        auto &d = it.second;
        if (d->device_id.get_short() == this->peer) {
            device = d;
            break;
        } else if (d->device_id.get_value() == this->peer) {
            device = d;
            break;
        } else if (d->name == this->peer) {
            device = d;
            break;
        }
    }
    if (!device) {
        log->warn("{}, device {} not found", actor.get_identity(), this->peer);
        return false;
    }
    if (device == actor.cluster_copy->get_device()) {
        log->warn("{}, folder {} cannot be share with local device ({})", actor.get_identity(), this->folder,
                  this->peer);
        return false;
    }

    actor.cmd_share_folder(folder, device);
    return true;
}

} // namespace syncspirit::daemon::command
