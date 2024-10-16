// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "finish_file_ack.h"

#include "../cluster_visitor.h"
#include "model/cluster.h"

using namespace syncspirit::model::diff::modify;

finish_file_ack_t::finish_file_ack_t(const model::file_info_t &file, std::string_view peer_id_) noexcept {
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    folder_id = folder->get_id();
    file_name = file.get_name();
    peer_id = peer_id_;
    assert(fi->get_device() == folder->get_cluster()->get_device().get());
}

auto finish_file_ack_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto peer = cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto my_folder = folder_infos.by_device(*cluster.get_device());
    auto peer_folder = folder_infos.by_device(*peer);
    auto &files = my_folder->get_file_infos();
    auto file = files.by_name(file_name);
    LOG_TRACE(log, "finish_file_ack for {}", file->get_full_name());

    uuid_t uuid;
    model::assign(uuid, file->get_uuid());
    auto source = peer_folder->get_file_infos().by_name(file_name);
    assert(source->is_locally_available());
    auto data = source->as_proto(false);
    auto seq = my_folder->get_max_sequence() + 1;
    data.set_sequence(seq);

    auto opt = file_info_t::create(uuid, data, my_folder);
    if (!opt) {
        return opt.assume_error();
    }
    file->update(*opt.assume_value());

    auto &blocks = source->get_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        assert(b);
        file->assign_block(b, i);
        file->mark_local_available(i);
    }
    file->mark_local();
    my_folder->add(file, true);

    return applicator_t::apply_sibling(cluster);
}

auto finish_file_ack_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_ack (visitor = {}), folder = {}, file = {}", (const void *)&visitor, folder_id,
              file_name);
    return visitor(*this, custom);
}
