// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "unshare_folder.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/error_code.h"
#include "utils/format.hpp"
#include "structs.pb.h"

using namespace syncspirit::model::diff::modify;

unshare_folder_t::unshare_folder_t(const model::cluster_t &cluster, std::string_view peer_device_,
                                   std::string_view folder_id_) noexcept
    : peer_id{peer_device_}, folder_id{folder_id_} {

    auto folder = cluster.get_folders().by_id(folder_id);
    auto peer = cluster.get_devices().by_sha256(peer_id);
    auto &blocks = cluster.get_blocks();

    auto &folder_infos = folder->get_folder_infos();
    auto folder_info = folder_infos.by_device_id(peer_id);
    folder_info_key = folder_info->get_key();

    auto &file_infos = folder_info->get_file_infos();
    keys_t removed_blocks;
    for (auto &fi : file_infos) {
        auto &file_info = *fi.item;
        removed_files.emplace(std::string(file_info.get_key()));
        for (auto &b : file_info.get_blocks()) {
            bool remove_block = true;
            for (auto &fb : b->get_file_blocks()) {
                if (*fb.file()->get_folder_info()->get_device() != *peer) {
                    remove_block = false;
                    break;
                }
            }
            if (remove_block) {
                removed_blocks.emplace(std::string(b->get_key()));
            }
        }
    }
    if (removed_blocks.size()) {
        inner_diff.reset(new remove_blocks_t(std::move(removed_blocks)));
    }
}

auto unshare_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto &devices = cluster.get_devices();

    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }

    auto &folder_infos = folder->get_folder_infos();
    auto folder_info = folder_infos.by_device_id(peer_id);
    if (!folder_info) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    folder_infos.remove(folder_info);

    LOG_TRACE(log, "applyging unshare_folder_t, folder {} with device {}", folder_id, peer->device_id());
    if (inner_diff) {
        return inner_diff->apply(cluster);
    }

    return outcome::success();
}

auto unshare_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting unshare_folder_t");
    return visitor(*this, custom);
}
