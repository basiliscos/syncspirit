// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "finish_file_ack.h"

#include "../cluster_visitor.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

finish_file_ack_t::finish_file_ack_t(const model::file_info_t &file, sequencer_t &sequencer) noexcept {
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    auto device = fi->get_device();
    assert(device != folder->get_cluster()->get_device().get());
    folder_id = folder->get_id();
    file_name = file.get_name();
    peer_id = fi->get_device()->device_id().get_sha256();
    proto_file = file.as_proto(false);

    auto local_file = file.local_file();
    if (!local_file) {
        uuid = sequencer.next_uuid();
    } else {
        assign(uuid, local_file->get_uuid());
    }
}

auto finish_file_ack_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto local_folder = folder_infos.by_device(*cluster.get_device());
    auto peer_folder = folder_infos.by_device(*peer);
    auto peer_file = peer_folder->get_file_infos().by_name(file_name);
    auto data_copy = proto_file;
    data_copy.set_sequence(local_folder->get_max_sequence() + 1);
    auto file_opt = file_info_t::create(uuid, data_copy, local_folder);
    if (!file_opt) {
        return file_opt.assume_error();
    }

    auto &files = local_folder->get_file_infos();
    auto local_file = files.by_name(file_name);
    if (!local_file) {
        local_file = std::move(file_opt.assume_value());
    } else {
        local_file->update(*file_opt.value());
    }
    LOG_TRACE(log, "finish_file_ack for {}", local_file->get_full_name());

    auto &blocks = peer_file->get_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        assert(b);
        local_file->assign_block(b, i);
        local_file->mark_local_available(i);
    }
    local_file->mark_local();
    local_folder->add_strict(local_file);

    local_file->notify_update();
    local_folder->notify_update();

    return applicator_t::apply_sibling(cluster);
}

auto finish_file_ack_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_ack (visitor = {}), folder = {}, file = {}", (const void *)&visitor, folder_id,
              file_name);
    return visitor(*this, custom);
}
