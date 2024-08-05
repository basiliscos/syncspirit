// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "clone_file.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

clone_file_t::clone_file_t(const model::file_info_t &source) noexcept
    : file{source.as_proto(false)}, has_blocks{!source.get_blocks().empty()} {

    auto peer_folder_info = source.get_folder_info();
    auto folder = peer_folder_info->get_folder();
    folder_id = folder->get_id();
    device_id = folder->get_cluster()->get_device()->device_id().get_sha256();
    peer_id = peer_folder_info->get_device()->device_id().get_sha256();
    assert(peer_id != device_id);

    auto my_folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto my_file = my_folder_info->get_file_infos().by_name(source.get_name());
    if (!my_file) {
        create_new_file = true;
        identical = false;
    } else if (has_blocks) {
        create_new_file = false;
        auto &my_blocks = my_file->get_blocks();
        auto &peer_blocks = source.get_blocks();

        if (my_blocks.size() != peer_blocks.size()) {
            identical = false;
        } else {
            identical = true;
            for (size_t i = 0; i < my_blocks.size(); ++i) {
                auto &bm = my_blocks[i];
                auto &bp = peer_blocks[i];
                if (bm->get_hash() != bp->get_hash()) {
                    identical = false;
                    break;
                }
            }
        }
    }
}

auto clone_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_my = folder->get_folder_infos().by_device_id(device_id);
    auto folder_peer = folder->get_folder_infos().by_device_id(peer_id);
    assert(folder_peer);
    auto &files = folder_my->get_file_infos();
    auto prev_file = file_info_ptr_t{};
    auto new_file = file_info_ptr_t{};
    uuid_t file_uuid;

    if (create_new_file) {
        file_uuid = cluster.next_uuid();
    } else {
        prev_file = files.by_name(file.name());
        model::assign(file_uuid, prev_file->get_uuid());
    }

    auto peer_file = folder_peer->get_file_infos().by_name(file.name());
    assert(peer_file);

    auto create_file = [&](bool inc_seq) {
        auto file_info = file;
        if (inc_seq) {
            auto seq = folder_my->get_max_sequence() + 1;
            folder_my->set_max_sequence(seq);
            file_info.set_sequence(seq);
        } else {
            file_info.set_sequence(0);
        }
        return file_info_t::create(file_uuid, file_info, folder_my);
    };

    if (!has_blocks) {
        auto opt = create_file(true);
        if (!opt) {
            return opt.assume_error();
        }
        new_file = std::move(opt.value());
    } else {
        if (create_new_file) {
            auto opt = create_file(false);
            if (!opt) {
                return opt.assume_error();
            }
            new_file = std::move(opt.value());
            new_file->set_source(peer_file);
        } else if (identical) {
            auto opt = create_file(true);
            if (!opt) {
                return opt.assume_error();
            }
            new_file = std::move(opt.value());
        } else {
            prev_file->set_source(peer_file);
        }
    }

    if (new_file) {
        auto &blocks = peer_file->get_blocks();
        for (size_t i = 0; i < blocks.size(); ++i) {
            auto &b = blocks[i];
            new_file->assign_block(b, i);
            if (identical) {
                new_file->mark_local_available(i);
            }
        }
        folder_my->add(new_file, false);
        LOG_TRACE(log, "clone_file_t, new file; folder = {}, name = {}, blocks = {}", folder_id, file.name(),
                  blocks.size());
    }

    return next ? next->apply(cluster) : outcome::success();
}

auto clone_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_file_t, folder = {}, file = {}", folder_id, file.name());
    return visitor(*this, custom);
}
