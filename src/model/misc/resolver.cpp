// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "resolver.h"
#include "model/cluster.h"
#include "model/folder_info.h"

#include <algorithm>

namespace syncspirit::model {

advance_action_t resolve(const file_info_t &remote, const file_info_t *local) noexcept {
    if (remote.is_unreachable()) {
        return advance_action_t::ignore;
    }
    if (remote.is_invalid()) {
        return advance_action_t::ignore;
    }

    auto remote_fi = remote.get_folder_info();
    auto folder = remote_fi->get_folder();
    auto &self = *folder->get_cluster()->get_device();
    auto &folder_infos = folder->get_folder_infos();
    auto &r_v = *remote.get_version();

    for (auto it : folder_infos) {
        auto fi = it.item.get();
        if (fi == remote_fi) {
            continue;
        }
        if (fi->get_device() == &self) {
            continue;
        }
        auto other_party_file = fi->get_file_infos().by_name(remote.get_name());
        if (other_party_file) {
            auto o_v = other_party_file->get_version();
            if (!r_v.contains(*o_v)) {
                return advance_action_t::ignore;
            }
        }
    }
    if (!local) {
        return advance_action_t::remote_copy;
    }
    // has not been scanned yet, should be re-checked later
    if (!local->is_local()) {
        return advance_action_t::ignore;
    }
    if (remote.is_deleted() && local->is_deleted()) {
        return advance_action_t::ignore;
    }

    auto &l_v = *local->get_version();

    auto &r_best = r_v.get_best();
    auto &l_best = l_v.get_best();
    auto rv = r_best.value();
    auto lv = l_best.value();

    // check posssible conflict
    if (r_best.id() == l_best.id()) {
        if (lv > rv) {
            return advance_action_t::ignore;
        } else if (lv < rv) {
            return advance_action_t::remote_copy;
        } else {
            return advance_action_t::ignore;
        }
    } else {
        auto r_superior = r_v.contains(l_v);
        auto l_superior = l_v.contains(r_v);
        auto concurrent = !r_superior && !l_superior;
        if (concurrent) {
            if (remote.is_deleted()) {
                return advance_action_t::ignore;
            } else if (local->is_deleted()) {
                return advance_action_t::remote_copy;
            }
        }
        if (r_superior) {
            return advance_action_t::remote_copy;
        }
        if (l_superior) {
            return advance_action_t::ignore;
        }

        if (rv > lv) {
            return advance_action_t::resolve_remote_win;
        } else if (lv > rv) {
            return advance_action_t::resolve_local_win;
        }

        auto remote_device = remote.get_folder_info()->get_device();
        auto local_device = local->get_folder_info()->get_device();
        auto remote_bytes = remote_device->device_id().get_sha256();
        auto local_bytes = local_device->device_id().get_sha256();
        assert(remote_bytes.size() == local_bytes.size());
        for (size_t j = 0; j < remote_bytes.size(); ++j) {
            auto rb = remote_bytes[j];
            auto lb = local_bytes[j];
            if (rb > lb) {
                return advance_action_t::resolve_remote_win;
            } else if (lb > rb) {
                return advance_action_t::resolve_local_win;
            }
        }
        assert(0 && "should not happen");
    }
    return advance_action_t::ignore;
}

advance_action_t resolve(const file_info_t &remote) noexcept {
    auto folder = remote.get_folder_info()->get_folder();
    auto self = folder->get_cluster()->get_device();
    auto local_folder = folder->get_folder_infos().by_device(*self);
    auto local_file = local_folder->get_file_infos().by_name(remote.get_name());
    return resolve(remote, local_file.get());
}

} // namespace syncspirit::model
