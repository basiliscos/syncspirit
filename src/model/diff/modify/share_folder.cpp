// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "share_folder.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"
#include "upsert_folder_info.h"
#include "remove_pending_folders.h"

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
            break;
        }
    }

    if (!index) {
        index = sequencer.next_uint64();
    }

    return new share_folder_t(sequencer.next_uuid(), peer.device_id().get_sha256(), folder.get_id(), index,
                              max_sequence, pending_folder);
}

share_folder_t::share_folder_t(const uuid_t &uuid, std::string_view device_id, std::string_view folder_id,
                               std::uint64_t index_id, std::int64_t max_sequence,
                               model::pending_folder_ptr_t uf) noexcept
    : peer_id(device_id) {
    auto current = assign_child(new upsert_folder_info_t(uuid, device_id, folder_id, index_id, max_sequence));
    if (uf) {
        auto keys = remove_pending_folders_t::keys_t{};
        keys.emplace_back(std::string{uf->get_key()});
        auto diff = cluster_diff_ptr_t{};
        current->assign_sibling(new remove_pending_folders_t(std::move(keys)));
    }
}

auto share_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging share_folder_t");
    return parent_t::apply_impl(cluster);
}

auto share_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting share_folder_t");
    return visitor(*this, custom);
}
