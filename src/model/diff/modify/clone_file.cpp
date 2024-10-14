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
    uuid_t uuid;

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
                           uuid_t uuid_) noexcept
    : proto_file{std::move(proto_file_)}, folder_id{folder_id_}, peer_id{peer_id_}, uuid{uuid_} {
    proto_file.set_sequence(0);
}

auto clone_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto my_device = cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_my = folder->get_folder_infos().by_device(*my_device);
    auto folder_peer = folder->get_folder_infos().by_device_id(peer_id);
    auto peer_file = folder_peer->get_file_infos().by_name(proto_file.name());
    auto &blocks = peer_file->get_blocks();
    assert(peer_file);

    auto prev_file = folder_my->get_file_infos().by_name(peer_file->get_name());
    auto file_opt = file_info_t::create(uuid, proto_file, folder_my);
    if (!file_opt) {
        return file_opt.assume_error();
    }

    auto file = std::move(file_opt.assume_value());
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        file->assign_block(b, i);
    }

    bool inc_sequence = false;
    if (prev_file) {
        bool refresh = peer_file->is_locally_available() || !prev_file->is_locally_available();
        if (refresh) {
            prev_file->update(*file);
        }
        file = std::move(prev_file);
        if (peer_file->is_locally_available() || (refresh && file->is_locally_available())) {
            inc_sequence = true;
        }
    } else {
        inc_sequence = blocks.empty();
    }

    file->mark_local();

    if (inc_sequence) {
        auto value = folder_my->get_max_sequence() + 1;
        file->set_sequence(value);
        folder_my->set_max_sequence(value);
    }

    if (!prev_file) {
        folder_my->add(file, false);
    }

    if (!inc_sequence) {
        file->set_source(peer_file);
    }
    LOG_TRACE(log, "clone_file_t, new file; folder = {}, name = {}, blocks = {}", folder_id, file->get_name(),
              blocks.size());

    if (auto iterator = folder_peer->get_device()->get_iterator(); iterator) {
        iterator->on_clone(std::move(peer_file));
    }

    return applicator_t::apply_sibling(cluster);
}

auto clone_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_file_t, folder = {}, file = {}", folder_id, proto_file.name());
    return visitor(*this, custom);
}

#if 0
clone_file_t::clone_file_t(const model::file_info_t &source, sequencer_t &sequencer) noexcept
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
    if (create_new_file) {
        uuid = sequencer.next_uuid();
    } else {
        assign(uuid, source.get_uuid());
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

    if (!create_new_file) {
        prev_file = files.by_name(file.name());
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
        return file_info_t::create(uuid, file_info, folder_my);
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
        new_file->mark_local();
        folder_my->add(new_file, false);
        LOG_TRACE(log, "clone_file_t, new file; folder = {}, name = {}, blocks = {}", folder_id, file.name(),
                  blocks.size());
    }

    if (auto iterator = folder_peer->get_device()->get_iterator(); iterator) {
        iterator->requeue_unchecked(std::move(peer_file));
    }

    return applicator_t::apply_sibling(cluster);
}

auto clone_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_file_t, folder = {}, file = {}", folder_id, file.name());
    return visitor(*this, custom);
}
#endif
