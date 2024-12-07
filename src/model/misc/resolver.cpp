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
    if (!remote.is_global()) {
        return advance_action_t::ignore;
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

    // check posssible conflict
    auto &r_v = remote.get_version();
    auto &l_v = local->get_version();
    auto r_sz = r_v.counters_size();
    auto l_sz = l_v.counters_size();
    auto v_limit = std::min(r_sz, l_sz);
    for (int i = 0; i < v_limit; ++i) {
        auto &remote_counter = r_v.counters(i);
        auto &local_counter = l_v.counters(i);
        auto rv = remote_counter.value();
        auto lv = local_counter.value();
        auto r_id = remote_counter.id();
        auto l_id = local_counter.id();

        if (r_id == l_id) {
            if (rv == lv) {
                continue;
            } else {
                if (lv > rv) {
                    return advance_action_t::ignore;
                } else {
                    return advance_action_t::remote_copy;
                }
            }
        } else {
            if (remote.is_deleted()) {
                return advance_action_t::ignore;
            } else if (local->is_deleted()) {
                return advance_action_t::remote_copy;
            } else {
                if (rv > lv) {
                    return advance_action_t::resolve_remote_win;
                } else if (lv > rv) {
                    return advance_action_t::resolve_local_win;
                } else {
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
                }
                assert(0 && "should not happen");
            }
        }
    }
    if (r_sz > l_sz) {
        return advance_action_t::remote_copy;
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
