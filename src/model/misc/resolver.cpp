// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "resolver.h"
#include "model/cluster.h"
#include "model/folder_info.h"
#include "proto/proto-helpers-bep.h"
#include "utils/platform.h"
#include <boost/nowide/convert.hpp>

namespace syncspirit::model {

static advance_action_t compare_by_version(const file_info_t &remote, const file_info_t &local) noexcept {
    auto &r_v = remote.get_version();
    auto &l_v = local.get_version();

    auto r_best = r_v.get_best();
    auto l_best = l_v.get_best();
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
            } else if (local.is_deleted()) {
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
        auto lm = local.get_modified_s();

        if (rm > lm) {
            return advance_action_t::resolve_remote_win;
        } else if (lm > rm) {
            return advance_action_t::ignore;
        }

        return r_id >= l_id ? advance_action_t::resolve_remote_win : advance_action_t::ignore;
    }
}

int compare(const file_info_t &file_1, const file_info_t &file_2) noexcept {
    if (file_1.is_deleted() && file_2.is_deleted()) {
        return 0;
    }
    auto action = compare_by_version(file_2, file_1);
    if (action == advance_action_t::ignore) {
        return 1;
    }
    return -1;
}

static advance_action_t _resolve(const file_info_t &remote, const file_info_t *local,
                                 const folder_info_t &local_folder) noexcept {
    if (remote.is_unreachable()) {
        return advance_action_t::ignore;
    }
    if (remote.is_invalid()) {
        return advance_action_t::ignore;
    }

    auto folder = local_folder.get_folder();
    auto &self = *folder->get_cluster()->get_device();
    auto &folder_infos = folder->get_folder_infos();
    auto &r_v = remote.get_version();

    for (auto it : folder_infos) {
        auto fi = it.item.get();
        if (fi == &local_folder) {
            continue;
        }
        auto other_party_file = fi->get_file_infos().by_name(remote.get_name()->get_full_name());
        if (other_party_file && other_party_file.get() != &remote) {
            auto o_v = other_party_file->get_version();
            if (!r_v.contains(o_v)) {
                return advance_action_t::ignore;
            }
        }
    }
    if (remote.is_deleted() && folder->is_deletion_ignored()) {
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

    return compare_by_version(remote, *local);
}

advance_action_t resolve(const file_info_t &remote, const file_info_t *local,
                         const folder_info_t &local_folder) noexcept {
    using P = utils::platform_t;
    if (remote.is_link() && !remote.is_deleted() && !P::symlinks_supported()) {
        return advance_action_t::ignore;
    }
    auto remote_name = remote.get_name()->get_full_name();
    if (!P::path_supported(bfs::path(boost::nowide::widen(remote_name)))) {
        return advance_action_t::ignore;
    }
#if 0
    auto folder = remote.get_folder_info()->get_folder();
    auto self = folder->get_cluster()->get_device();
    auto local_folder = folder->get_folder_infos().by_device(*self);
    auto &local_files = local_folder->get_file_infos();
    auto local_file = local_files.by_name(remote_name);
#endif
    auto action = _resolve(remote, local, local_folder);
    if (action == advance_action_t::resolve_remote_win) {
        auto name = remote.get_name()->get_own_name();
        if (name.find(".sync-conflict-") != std::string::npos) {
            action = advance_action_t::ignore;
        } else {
            auto resolved_name = local->make_conflicting_name();
            if (auto resolved = local_folder.get_file_infos().by_name(resolved_name); resolved) {
                action = advance_action_t::ignore;
            }
        }
    }
    return action;
}

} // namespace syncspirit::model
