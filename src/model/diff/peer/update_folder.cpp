// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "update_folder.h"
#include "model/misc/file_iterator.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff::peer;

update_folder_t::update_folder_t(std::string_view folder_id_, std::string_view peer_id_, files_t files_, uuids_t uuids,
                                 blocks_t blocks, orphaned_blocks_t::set_t removed_blocks) noexcept
    : folder_id{std::string(folder_id_)}, peer_id{std::string(peer_id_)}, files{std::move(files_)},
      uuids{std::move(uuids)} {
    auto current = (cluster_diff_t *)(nullptr);
    if (!blocks.empty()) {
        current = assign_child(new modify::add_blocks_t(std::move(blocks)));
    }
    if (!removed_blocks.empty()) {
        auto ptr = new modify::remove_blocks_t(std::move(removed_blocks));
        if (current) {
            current->assign_sibling(ptr);
        } else {
            assign_sibling(ptr);
        }
    }
}

auto update_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster);
    if (!r) {
        return r;
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(peer_id);
    auto &bm = cluster.get_blocks();

    auto max_seq = folder_info->get_max_sequence();
    auto &fm = folder_info->get_file_infos();

    for (std::size_t i = 0; i < files.size(); ++i) {
        auto &f = files[i];
        auto &uuid = uuids[i];
        auto file = file_info_ptr_t{};
        auto opt = file_info_t::create(uuid, f, folder_info);
        if (!opt) {
            return opt.assume_error();
        }
        file = std::move(opt.assume_value());

        if (f.size()) {
            for (int i = 0; i < f.blocks_size(); ++i) {
                auto &b = f.blocks(i);
                auto strict_hash = block_info_t::make_strict_hash(b.hash());
                auto block = bm.get(strict_hash.get_hash());
                assert(block);
                file->assign_block(block, (size_t)i);
            }
        }
        if (auto prev_file = fm.by_name(file->get_name()); prev_file) {
            prev_file->update(*file);
            file = std::move(prev_file);
        }

        folder_info->add_strict(file);
    }

    folder->notify_update();

    LOG_TRACE(log, "update_folder_t, apply(); max seq: {} -> {}", max_seq, folder_info->get_max_sequence());

    r = applicator_t::apply_sibling(cluster);

    if (auto iterator = folder_info->get_device()->get_iterator(); iterator) {
        iterator->on_upsert(folder_info);
    }

    folder_info->notify_update();
    return r;
}

auto update_folder_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_folder_t", folder_id);
    return visitor(*this, custom);
}

using diff_t = diff::cluster_diff_ptr_t;

static auto construct(sequencer_t &sequencer, folder_info_ptr_t &folder_info, std::string_view peer_id,
                      update_folder_t::files_t files, update_folder_t::blocks_t new_blocks) -> outcome::result<diff_t> {
    auto folder = folder_info->get_folder();

    auto uuids = update_folder_t::uuids_t{};
    auto max_seq = folder_info->get_max_sequence();
    uuids.reserve(files.size());
    auto &fm = folder_info->get_file_infos();
    auto orphaned_candidates = orphaned_blocks_t{};
    for (const auto &f : files) {
        auto file = file_info_ptr_t{};
        bu::uuid file_uuid;
        auto prev_file = fm.by_name(f.name());
        if (prev_file) {
            assign(file_uuid, prev_file->get_uuid());
            orphaned_candidates.record(*prev_file);
        } else {
            file_uuid = sequencer.next_uuid();
        }
        uuids.emplace_back(file_uuid);
    }

    auto orphaned_blocks = orphaned_candidates.deduce();
    if (!orphaned_blocks.empty()) {
        for (auto &b : new_blocks) {
            auto strict_hash = block_info_t::make_strict_hash(b.hash());
            auto it = orphaned_blocks.find(strict_hash.get_key());
            if (it != orphaned_blocks.end()) {
                orphaned_blocks.erase(it);
            }
        }
    }

    auto diff = diff_t(new update_folder_t(folder->get_id(), peer_id, std::move(files), std::move(uuids),
                                           std::move(new_blocks), std::move(orphaned_blocks)));
    return outcome::success(std::move(diff));
}

template <typename T>
static auto instantiate(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                        const T &message) noexcept -> outcome::result<diff_t> {
    auto folder = cluster.get_folders().by_id(message.folder());
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto device_id = source.device_id().get_sha256();
    auto fi = folder->get_folder_infos().by_device_id(device_id);
    if (!fi) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    auto log = update_folder_t::get_log();
    auto max_seq = fi->get_max_sequence();
    auto &blocks = cluster.get_blocks();
    update_folder_t::files_t files;
    update_folder_t::blocks_t new_blocks;
    files.reserve(static_cast<size_t>(message.files_size()));
    for (int i = 0; i < message.files_size(); ++i) {
        auto &f = message.files(i);
        if (f.deleted() && f.blocks_size()) {
            LOG_WARN(log, "file {} should not have blocks", f.name());
            return make_error_code(error_code_t::unexpected_blocks);
        }
        if (f.sequence() <= 0) {
            LOG_WARN(log, "file '{}' has wrong sequnce", f.name(), f.sequence());
            return make_error_code(error_code_t::invalid_sequence);
        }
        for (int j = 0; j < f.blocks_size(); ++j) {
            auto &b = f.blocks(j);
            auto strict_hash = block_info_t::make_strict_hash(b.hash());
            if (!blocks.get(strict_hash.get_hash())) {
                new_blocks.emplace_back(std::move(b));
            }
        }
        if (!f.version().counters_size()) {
            return make_error_code(error_code_t::missing_version);
        }
        files.emplace_back(std::move(message.files(i)));
    }

    return construct(sequencer, fi, device_id, std::move(files), std::move(new_blocks));
}

auto update_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, const model::device_t &source,
                             const proto::Index &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, sequencer, source, message);
}

auto update_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, const model::device_t &source,
                             const proto::IndexUpdate &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, sequencer, source, message);
}
