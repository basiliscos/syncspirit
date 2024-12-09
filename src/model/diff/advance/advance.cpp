// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "advance.h"
#include "remote_copy.h"
#include "model/cluster.h"
#include "model/misc/orphaned_blocks.h"
#include "model/diff/modify/remove_blocks.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff::advance;

static std::string_view stringify(advance_action_t action) {
    if (action == advance_action_t::remote_copy) {
        return "remote_copy";
    } else if (action == advance_action_t::local_update) {
        return "local_update";
    } else if (action == advance_action_t::resolve_remote_win) {
        return "resolve_remote_win";
    } else if (action == advance_action_t::resolve_local_win) {
        return "resolve_local_win";
    }
    return "ignore";
}

auto advance_t::create(const model::file_info_t &source, sequencer_t &sequencer) noexcept -> cluster_diff_ptr_t {
    auto &cluster = *source.get_folder_info()->get_folder()->get_cluster();
    auto proto_file = source.as_proto(false);
    auto peer_folder_info = source.get_folder_info();
    auto folder = peer_folder_info->get_folder();
    auto folder_id = folder->get_id();
    auto peer_id = peer_folder_info->get_device()->device_id().get_sha256();
    return new remote_copy_t(cluster, sequencer, std::move(proto_file), folder_id, peer_id);
}

advance_t::advance_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                     std::string_view folder_id_, std::string_view peer_id_, advance_action_t action_) noexcept
    : proto_file{std::move(proto_file_)}, folder_id{folder_id_}, peer_id{peer_id_}, action{action_} {
    proto_file.set_sequence(0);

    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_id = folder->get_id();
    auto device_id = cluster.get_device()->device_id().get_sha256();
    assert(peer_id != device_id);

    auto &local_folder_infos = folder->get_folder_infos();
    auto local_folder_info = local_folder_infos.by_device_id(device_id);
    auto &local_files = local_folder_info->get_file_infos();
    auto local_file = local_files.by_name(proto_file.name());

    auto possibly_orphaned_blocks = orphaned_blocks_t::set_t();

    if (!local_file) {
        uuid = sequencer.next_uuid();
    } else {
        assign(uuid, local_file->get_uuid());
        auto orphaned = orphaned_blocks_t();
        orphaned.record(*local_file);
        possibly_orphaned_blocks = orphaned.deduce();
        for (int i = 0; i < proto_file.blocks_size(); ++i) {
            auto &b = proto_file.blocks(i);
            auto strict_hash = block_info_t::make_strict_hash(b.hash());
            auto it = possibly_orphaned_blocks.find(strict_hash.get_key());
            if (it != possibly_orphaned_blocks.end()) {
                possibly_orphaned_blocks.erase(it);
            }
        }
    }

    if (!possibly_orphaned_blocks.empty()) {
        assign_child(new modify::remove_blocks_t(std::move(possibly_orphaned_blocks)));
    }
}

auto advance_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }
    auto my_device = cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto local_folder = folder->get_folder_infos().by_device(*my_device);
    auto peer_folder = folder->get_folder_infos().by_device_id(peer_id);
    auto peer_file = peer_folder->get_file_infos().by_name(proto_file.name());
    assert(peer_file);

    auto prev_file = local_folder->get_file_infos().by_name(peer_file->get_name());
    auto local_file_opt = file_info_t::create(uuid, proto_file, local_folder);
    if (!local_file_opt) {
        return local_file_opt.assume_error();
    }

    auto local_file = std::move(local_file_opt.assume_value());

    if (prev_file) {
        local_folder->get_file_infos().remove(prev_file);
        prev_file->update(*local_file);
        local_file = std::move(prev_file);
    }

    auto &blocks = peer_file->get_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        assert(b);
        local_file->assign_block(b, i);
        local_file->mark_local_available(i);
    }

    auto seqeuence = local_folder->get_max_sequence() + 1;
    local_file->mark_local();
    local_file->set_sequence(seqeuence);
    local_folder->add_strict(local_file);

    LOG_TRACE(log, "advance_t ({}), folder = {}, name = {}, blocks = {}, seq. = {}", stringify(action), folder_id,
              local_file->get_name(), blocks.size(), seqeuence);

    local_file->notify_update();
    local_folder->notify_update();

    return applicator_t::apply_sibling(cluster);
}
