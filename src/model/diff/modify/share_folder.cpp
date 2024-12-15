// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "share_folder.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"
#include "add_remote_folder_infos.h"
#include "remove_pending_folders.h"
#include "upsert_folder_info.h"
#include "utils/format.hpp"

using namespace syncspirit::model::diff::modify;

auto share_folder_t::create(cluster_t &cluster, sequencer_t &sequencer, const model::device_t &peer,
                            const model::folder_t &folder) noexcept -> outcome::result<cluster_diff_ptr_t> {
    auto folder_info = folder.get_folder_infos().by_device(peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto &pending = cluster.get_pending_folders();
    auto index = uint64_t{0};
    auto max_sequence = int64_t{0};

    auto pending_folder = model::pending_folder_ptr_t{};
    for (auto it = pending.begin(); it != pending.end(); ++it) {
        auto &uf = *it->item;
        if (uf.device_id() == peer.device_id() && uf.get_id() == folder.get_id()) {
            pending_folder = it->item;
            index = pending_folder->get_index();
            break;
        }
    }

    return new share_folder_t(sequencer.next_uuid(), peer, folder.get_id(), index, pending_folder);
}

share_folder_t::share_folder_t(const bu::uuid &uuid, const model::device_t &peer, std::string_view folder_id_,
                               std::uint64_t index_id, model::pending_folder_ptr_t pf) noexcept
    : peer_id(peer.device_id().get_sha256()), folder_id{folder_id_} {
    LOG_DEBUG(log, "share_folder_t, with peer = {}, folder_id = {}", peer.device_id(), folder_id);
    auto current = assign_child(new upsert_folder_info_t(uuid, peer_id, folder_id, index_id));
    if (pf) {
        using container_t = typename add_remote_folder_infos_t::container_t;
        using item_t = typename container_t::value_type;
        auto keys = remove_pending_folders_t::keys_t{};
        keys.emplace_back(std::string{pf->get_key()});
        auto diff = cluster_diff_ptr_t{};
        current = current->assign_sibling(new remove_pending_folders_t(std::move(keys)));

        auto remote_folders = container_t{{item_t{folder_id, index_id, 0}}};
        current = current->assign_sibling(new add_remote_folder_infos_t(peer, std::move(remote_folders)));
    }
}

auto share_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging share_folder_t");
    return applicator_t::apply_impl(cluster);
}

auto share_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting share_folder_t");
    return visitor(*this, custom);
}
