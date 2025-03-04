// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "advance.h"
#include "remote_copy.h"
#include "remote_win.h"
#include "model/cluster.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/misc/orphaned_blocks.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff::advance;

static std::string_view stringify(advance_action_t action) {
    if (action == advance_action_t::remote_copy) {
        return "remote_copy";
    } else if (action == advance_action_t::local_update) {
        return "local_update";
    } else if (action == advance_action_t::resolve_remote_win) {
        return "resolve_remote_win";
    }
    return "ignore";
}

auto advance_t::create(advance_action_t action, const model::file_info_t &source, sequencer_t &sequencer) noexcept
    -> cluster_diff_ptr_t {
    auto &cluster = *source.get_folder_info()->get_folder()->get_cluster();
    auto proto_file = source.as_proto(true);
    auto peer_folder_info = source.get_folder_info();
    auto folder = peer_folder_info->get_folder();
    auto folder_id = folder->get_id();
    auto peer_id = peer_folder_info->get_device()->device_id().get_sha256();

    if (action == advance_action_t::remote_copy) {
        return new remote_copy_t(cluster, sequencer, std::move(proto_file), folder_id, peer_id);
    } else {
        assert(action == advance_action_t::resolve_remote_win);
        return new remote_win_t(cluster, sequencer, std::move(proto_file), folder_id, peer_id);
    }
}

advance_t::advance_t(std::string_view folder_id_, std::string_view peer_id_, advance_action_t action_,
                     bool disable_blocks_removal_) noexcept
    : folder_id{folder_id_}, peer_id{peer_id_}, action{action_}, disable_blocks_removal{disable_blocks_removal_} {}

void advance_t::initialize(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_source_,
                           std::string_view local_file_name_) noexcept {
    proto_source = std::move(proto_source_);
    assert(!(proto_source.blocks_size() && proto_source.deleted()));
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_id = folder->get_id();
    auto &self = *cluster.get_device();
    auto self_id = self.device_id().get_sha256();

    auto &local_folder_infos = folder->get_folder_infos();
    auto local_folder_info = local_folder_infos.by_device_id(self_id);
    auto &local_files = local_folder_info->get_file_infos();
    auto local_file = local_files.by_name(local_file_name_);

    auto orphans = orphaned_blocks_t::set_t();
    proto_local = proto_source;
    proto_local.set_sequence(0);
    proto_local.set_name(std::string(local_file_name_));

    if (!local_file) {
        uuid = sequencer.next_uuid();
    } else if (!disable_blocks_removal) {
        assign(uuid, local_file->get_uuid());
        auto orphaned = orphaned_blocks_t();
        orphaned.record(*local_file);
        orphans = orphaned.deduce();
    }

    auto new_blocks = modify::add_blocks_t::blocks_t{};
    auto &blocks_map = cluster.get_blocks();
    if (proto_local.size()) {
        for (int i = 0; i < proto_local.blocks_size(); ++i) {
            auto &b = proto_local.blocks(i);
            auto strict_hash = block_info_t::make_strict_hash(b.hash());
            auto block = blocks_map.get(strict_hash.get_hash());
            if (!block) {
                new_blocks.push_back(b);
            } else {
                auto it = orphans.find(strict_hash.get_key());
                if (it != orphans.end()) {
                    orphans.erase(it);
                }
            }
        }
    }

    LOG_DEBUG(log, "advance_t ({}), folder = {}, name = {} ( -> {}), blocks = {}, removed blocks = {}, new blocks = {}",
              stringify(action), folder_id, proto_source.name(), proto_local.name(), proto_local.blocks_size(),
              orphans.size(), new_blocks.size());

    auto current = (cluster_diff_t *){};
    if (!new_blocks.empty()) {
        assert(action == advance_action_t::local_update);
        current = assign_child(new modify::add_blocks_t(std::move(new_blocks)));
    }

    if (!orphans.empty()) {
        auto diff = new modify::remove_blocks_t(std::move(orphans));
        if (current) {
            current->assign_sibling(diff);
        } else {
            assign_child(diff);
        }
    }
}

auto advance_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster, controller);
    if (!r) {
        return r;
    }
    auto my_device = cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto local_folder = folder->get_folder_infos().by_device(*my_device);
    auto peer_folder = folder->get_folder_infos().by_device_id(peer_id);

    auto prev_file = local_folder->get_file_infos().by_name(proto_local.name());
    auto local_file_opt = file_info_t::create(uuid, proto_local, local_folder);
    if (!local_file_opt) {
        return local_file_opt.assume_error();
    }
    auto local_file = std::move(local_file_opt.assume_value());
    if (prev_file) {
        local_folder->get_file_infos().remove(prev_file);
        prev_file->update(*local_file);
        local_file = std::move(prev_file);
    }

    if (proto_local.size()) {
        auto &blocks_map = cluster.get_blocks();
        for (int i = 0; i < proto_local.blocks_size(); ++i) {
            auto &block = proto_local.blocks(i);
            auto strict_hash = block_info_t::make_strict_hash(block.hash());
            auto block_info = blocks_map.get(strict_hash.get_hash());
            assert(block_info);
            local_file->assign_block(block_info, i);
            local_file->mark_local_available(i);
        }
    }

    auto sequence = local_folder->get_max_sequence() + 1;
    local_file->mark_local();
    local_file->set_sequence(sequence);
    local_folder->add_strict(local_file);

    LOG_TRACE(log, "advance_t ({}), folder = {}, name = {}, blocks = {}, seq. = {}", stringify(action), folder_id,
              local_file->get_name(), proto_local.blocks_size(), sequence);

    local_file->notify_update();
    local_folder->notify_update();

    return applicator_t::apply_sibling(cluster, controller);
}
