// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "share_folder.h"
#include "pair_iterator.h"
#include "../governor_actor.h"
#include "../error_code.h"
#include "model/diff/modify/share_folder.h"

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
    using namespace model::diff;
    log = actor.log;
    model::folder_ptr_t folder;
    for (auto it : actor.cluster->get_folders()) {
        auto &f = it.item;
        if ((f->get_id() == this->folder)) {
            folder = f;
            break;
        }
        if (f->get_label() == this->folder) {
            folder = f;
            break;
        }
    }
    if (!folder) {
        log->warn("{}, folder {} not found", actor.get_identity(), this->folder);
        return false;
    }

    model::device_ptr_t device;
    for (auto it : actor.cluster->get_devices()) {
        auto &d = it.item;
        if (d->device_id().get_short() == this->peer) {
            device = d;
            break;
        } else if (d->device_id().get_value() == this->peer) {
            device = d;
            break;
        } else if (d->get_name() == this->peer) {
            device = d;
            break;
        }
    }
    if (!device) {
        log->warn("{}, device {} not found", actor.get_identity(), this->peer);
        return false;
    }
    if (device == actor.cluster->get_device()) {
        log->warn("{}, folder {} cannot be share with local device ({})", actor.get_identity(), this->folder,
                  this->peer);
        return false;
    }
    auto fi = folder->get_folder_infos().by_device(*device);
    if (fi) {
        log->warn("{}, folder {} is already shared with {}", actor.get_identity(), this->folder, this->peer);
        return false;
    }

    auto opt = modify::share_folder_t::create(*actor.cluster, *actor.sequencer, *device, *folder);
    if (!opt) {
        log->warn("{}, cannot share: ", actor.get_identity(), opt.assume_error().message());
        return false;
    }
    actor.send<model::payload::model_update_t>(actor.coordinator, std::move(opt.value()), &actor);
    return false;
}

} // namespace syncspirit::daemon::command
