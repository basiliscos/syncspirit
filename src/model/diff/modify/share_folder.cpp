// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "share_folder.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"
#include "structs.pb.h"
#include "upsert_folder_info.h"
#include "remove_unknown_folders.h"

using namespace syncspirit::model::diff::modify;

auto share_folder_t::create(cluster_t &cluster, sequencer_t &sequencer, const model::device_t& peer, const model::folder_t& folder) noexcept
-> outcome::result<cluster_diff_ptr_t>
{
    auto folder_info = folder.get_folder_infos().by_device(peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto &unknown = cluster.get_unknown_folders();
    auto index = uint64_t{0};
    auto max_sequence = int64_t{0};

    auto unknown_folder = model::unknown_folder_ptr_t{};
    for (auto it = unknown.begin(); it != unknown.end(); ++it) {
        auto &uf = *it->item;
        if (uf.device_id() == peer.device_id() && uf.get_id() == folder.get_id()) {
            index = uf.get_index();
            max_sequence = uf.get_max_sequence();
            unknown_folder = it->item;
            break;
        }
    }

    if (!index) {
        index = sequencer.next_uint64();
    }

    return new share_folder_t(sequencer.next_uuid(), peer.device_id().get_sha256(), folder.get_id(), index, max_sequence, unknown_folder);
}

share_folder_t::share_folder_t(const uuid_t &uuid, std::string_view device_id, std::string_view folder_id,
                               std::uint64_t index_id, std::int64_t max_sequence, model::unknown_folder_ptr_t uf) noexcept:
    peer_id(device_id) {
    auto current = assign_child(new upsert_folder_info_t(uuid, device_id, folder_id, index_id, max_sequence));
    if (uf) {
        auto keys = remove_unknown_folders_t::keys_t{};
        keys.emplace_back(std::string{uf->get_key()});
        auto diff = cluster_diff_ptr_t{};
        current->assign_sibling(new remove_unknown_folders_t(std::move(keys)));
    }

}

auto share_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging share_folder_t");

    return parent_t::apply_impl(cluster);
#if 0
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto &devices = cluster.get_devices();
    auto &unknown = cluster.get_unknown_folders();

    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::no_such_device);
    }
    auto index = uint64_t{0};
    auto max_sequence = int64_t{0};
    auto unknown_folder = model::unknown_folder_ptr_t{};

    for (auto it = unknown.begin(); it != unknown.end(); ++it) {
        auto &uf = *it->item;
        if (uf.device_id() == peer->device_id() && uf.get_id() == folder_id) {
            index = uf.get_index();
            max_sequence = uf.get_max_sequence();
            unknown_folder = it->item;
            break;
        }
    }
    LOG_TRACE(log, "applyging share_folder_t, folder {} with device {}, index = {}, max sequence = {}", folder_id,
              peer->device_id(), index, max_sequence);

    auto folder_info = folder->get_folder_infos().by_device(*peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto db = db::FolderInfo();
    db.set_index_id(index);
    db.set_max_sequence(max_sequence);
    auto fi_opt = folder_info_t::create(cluster.next_uuid(), db, peer, folder);
    if (!fi_opt) {
        return fi_opt.assume_error();
    }

    auto &fi = fi_opt.value();
    folder->add(fi);
    if (index) {
        unknown.remove(unknown_folder);
    }
#endif
    return applicator_t::apply_sibling(cluster);
}

auto share_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting share_folder_t");
    return visitor(*this, custom);
}
