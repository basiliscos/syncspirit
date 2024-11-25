// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "clone_file.h"
#include "../cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/file_iterator.h"

using namespace syncspirit::model::diff::modify;

auto clone_file_t::create(const model::file_info_t &source, sequencer_t &sequencer) noexcept -> cluster_diff_ptr_t {
    auto proto_file = source.as_proto(false);
    auto peer_folder_info = source.get_folder_info();
    auto folder = peer_folder_info->get_folder();
    auto folder_id = folder->get_id();
    auto device_id = folder->get_cluster()->get_device()->device_id().get_sha256();
    auto peer_id = peer_folder_info->get_device()->device_id().get_sha256();
    assert(peer_id != device_id);

    auto &my_folder_infos = folder->get_folder_infos();
    auto my_folder_info = my_folder_infos.by_device_id(device_id);
    auto &my_files = my_folder_info->get_file_infos();
    auto my_file = my_files.by_name(source.get_name());
    bu::uuid uuid;

    if (!my_file) {
        uuid = sequencer.next_uuid();
    } else {
        assign(uuid, my_file->get_uuid());
    }

    auto diff = cluster_diff_ptr_t{};
    diff.reset(new clone_file_t(std::move(proto_file), folder_id, peer_id, uuid));
    return diff;
}

clone_file_t::clone_file_t(proto::FileInfo proto_file_, std::string_view folder_id_, std::string_view peer_id_,
                           bu::uuid uuid_) noexcept
    : proto_file{std::move(proto_file_)}, folder_id{folder_id_}, peer_id{peer_id_}, uuid{uuid_} {
    proto_file.set_sequence(0);
}

auto clone_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
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

    LOG_TRACE(log, "clone_file_t, folder = {}, name = {}, blocks = {}, seq. = {}", folder_id, local_file->get_name(),
              blocks.size(), seqeuence);

    if (auto iterator = peer_folder->get_device()->get_iterator(); iterator) {
        iterator->commit_sync(std::move(peer_file));
    }

    local_file->notify_update();
    local_folder->notify_update();

    return applicator_t::apply_sibling(cluster);
}

auto clone_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_file_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
}
