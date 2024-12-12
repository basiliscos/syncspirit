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
    auto &r_v = remote.get_version();
    auto r_sz = r_v.counters_size();
    auto r_best_counter = proto::Counter();
    for (int i = 0; i < r_sz; ++i) {
        auto &counter = r_v.counters(i);
        if (counter.value() > r_best_counter.value()) {
            r_best_counter = counter;
        }
    }

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
            auto &o_v = other_party_file->get_version();
            for (int i = 0; i < o_v.counters_size(); ++i) {
                auto &counter = o_v.counters(i);
                if (counter.value() > r_best_counter.value()) {
                    return advance_action_t::ignore;
                }
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

    auto &l_v = local->get_version();
    auto l_sz = l_v.counters_size();
    auto l_best_counter = proto::Counter();
    for (int i = 0; i < l_sz; ++i) {
        auto &counter = l_v.counters(i);
        if (counter.value() > l_best_counter.value()) {
            l_best_counter = counter;
        }
    }

    auto unpack = [&](auto &counter) -> std::pair<uint64_t, uint64_t> {
        return std::make_pair(counter.id(), counter.value());
    };
    auto [r_id, rv] = unpack(r_best_counter);
    auto [l_id, lv] = unpack(l_best_counter);

    for (int i = 0; i < r_sz; ++i) {
        auto &counter = r_v.counters(i);
        if (counter.id() == l_id && counter.value() == lv) {
            r_best_counter = counter;
        }
    }

    // check posssible conflict
    if (r_id == l_id) {
        if (lv > rv) {
            return advance_action_t::ignore;
        } else if (lv < rv) {
            return advance_action_t::remote_copy;
        } else {
            return advance_action_t::ignore;
        }
    } else {
        if (remote.is_deleted()) {
            return advance_action_t::ignore;
        } else if (local->is_deleted()) {
            return advance_action_t::remote_copy;
        } else {
            // remote version already covers local version
            for (int i = 0; i < r_sz; ++i) {
                auto &counter = r_v.counters(i);
                if (counter.id() == l_id && counter.value() == lv) {
                    return advance_action_t::remote_copy;
                }
            }

            // local version already covers remove version
            for (int i = 0; i < l_sz; ++i) {
                auto &counter = l_v.counters(i);
                if (counter.id() == r_id && counter.value() == rv) {
                    return advance_action_t::ignore;
                }
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
        }
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
