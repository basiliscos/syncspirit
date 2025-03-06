// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "resolver.h"
#include "model/cluster.h"
#include "model/folder_info.h"
#include "proto/proto-helpers.h"

namespace syncspirit::model {

static advance_action_t resolve(const file_info_t &remote, const file_info_t *local) noexcept {
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
    auto rv = proto::get_value(r_best);
    auto lv = proto::get_value(l_best);
    auto r_id = proto::get_id(r_best);
    auto l_id = proto::get_id(l_best);

    // check possible conflict
    if (r_id == l_id) {
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

        auto rm = remote.get_modified_s();
        auto lm = local->get_modified_s();

        if (rm > lm) {
            return advance_action_t::resolve_remote_win;
        } else if (lm > rm) {
            return advance_action_t::ignore;
        }

        return r_id >= l_id ? advance_action_t::resolve_remote_win : advance_action_t::ignore;
    }
    return advance_action_t::ignore;
}

advance_action_t resolve(const file_info_t &remote) noexcept {
    auto folder = remote.get_folder_info()->get_folder();
    auto self = folder->get_cluster()->get_device();
    auto local_folder = folder->get_folder_infos().by_device(*self);
    auto &local_files = local_folder->get_file_infos();
    auto local_file = local_files.by_name(remote.get_name());
    auto action = resolve(remote, local_file.get());
    if (action == advance_action_t::resolve_remote_win) {
        auto name = remote.get_path().filename().string();
        if (name.find(".sync-conflict-") != std::string::npos) {
            action = advance_action_t::ignore;
        } else {
            auto resolved_name = local_file->make_conflicting_name();
            if (auto resolved = local_files.by_name(resolved_name); resolved) {
                action = advance_action_t::ignore;
            }
        }
    }
    return action;
}

} // namespace syncspirit::model
