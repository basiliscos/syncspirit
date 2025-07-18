// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "update_folder.h"
#include "model/misc/file_iterator.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"
#include "proto/proto-helpers-bep.h"

#include <memory_resource>
#include <set>

using namespace syncspirit;
using namespace syncspirit::model;
using namespace syncspirit::model::diff::peer;

update_folder_t::update_folder_t(std::string_view folder_id_, utils::bytes_view_t peer_id_, files_t files_,
                                 uuids_t uuids, blocks_t blocks, orphaned_blocks_t::set_t removed_blocks) noexcept
    : folder_id{std::string(folder_id_)}, peer_id{peer_id_.begin(), peer_id_.end()}, files(std::move(files_)),
      uuids{std::move(uuids)} {
    LOG_DEBUG(log, "update_folder_t, folder = {}", folder_id);
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

auto update_folder_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    auto r = applicator_t::apply_child(cluster, controller);
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

        if (proto::get_size(f)) {
            auto blocks_count = proto::get_blocks_size(f);
            for (int i = 0; i < blocks_count; ++i) {
                auto &b = proto::get_blocks(f, i);
                auto hash = proto::get_hash(b);
                auto strict_hash = block_info_t::make_strict_hash(hash);
                auto block = bm.by_hash(strict_hash.get_hash());
                assert(block);
                file->assign_block(block, (size_t)i);
            }
        }
        if (auto prev_file = fm.by_name(file->get_name()); prev_file) {
            fm.remove(prev_file);
            prev_file->update(*file);
            file = std::move(prev_file);
            file->notify_update();
        }

        folder_info->add_strict(file);
    }

    LOG_TRACE(log, "update_folder_t, apply(); max seq: {} -> {}", max_seq, folder_info->get_max_sequence());

    r = applicator_t::apply_sibling(cluster, controller);

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

static auto construct(sequencer_t &sequencer, folder_info_ptr_t &folder_info, syncspirit::utils::bytes_view_t peer_id,
                      update_folder_t::files_t files, update_folder_t::blocks_t new_blocks) -> outcome::result<diff_t> {
    auto folder = folder_info->get_folder();

    auto uuids = update_folder_t::uuids_t{};
    uuids.reserve(files.size());
    auto &fm = folder_info->get_file_infos();
    auto orphaned_candidates = orphaned_blocks_t{};
    auto white_listed_blocks = orphaned_blocks_t::set_t{};
    for (const auto &f : files) {
        auto blocks_sz = proto::get_blocks_size(f);
        for (size_t i = 0; i < blocks_sz; ++i) {
            auto &b = proto::get_blocks(f, i);
            auto hash = proto::get_hash(b);
            auto strict_hash = block_info_t::make_strict_hash(hash);
            auto owned_hash = utils::bytes_t(strict_hash.get_hash());
            white_listed_blocks.emplace(owned_hash);
        }
        auto file = file_info_ptr_t{};
        bu::uuid file_uuid;
        auto prev_file = fm.by_name(proto::get_name(f));
        if (prev_file) {
            assign(file_uuid, prev_file->get_uuid());
            orphaned_candidates.record(*prev_file);
        } else {
            file_uuid = sequencer.next_uuid();
        }
        uuids.emplace_back(file_uuid);
    }

    auto orphaned_blocks = orphaned_candidates.deduce(white_listed_blocks);
    if (!orphaned_blocks.empty()) {
        for (auto &b : new_blocks) {
            auto hash = proto::get_hash(b);
            auto strict_hash = block_info_t::make_strict_hash(hash);
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

static auto instantiate(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                        const syncspirit::proto::IndexBase &message) noexcept -> outcome::result<diff_t> {
    using skipped_file_indices_t = std::pmr::set<size_t>;
    using seen_file_names_t = std::pmr::unordered_set<std::string_view>;
    using unique_blocks_t = std::pmr::unordered_set<std::pmr::string>;
    auto buffer = std::array<std::byte, 10 * 1024>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    auto folder_id = proto::get_folder(message);
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto device_id = source.device_id().get_sha256();
    auto fi = folder->get_folder_infos().by_device_id(device_id);
    if (!fi) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    auto &prev_files = fi->get_file_infos();
    auto log = update_folder_t::get_log();
    auto &blocks = cluster.get_blocks();
    auto files = update_folder_t::files_t();
    auto unique_new_blocks = unique_blocks_t(allocator);
    auto new_blocks = update_folder_t::blocks_t();
    auto prev_sequence = fi->get_max_sequence();
    auto files_count = proto::get_files_size(message);
    files.reserve(files_count);

    auto seen_file_names = seen_file_names_t();
    auto skipp_file_indices = skipped_file_indices_t();

    // pass 1: skip "outdated" files and pick only recent ones
    for (int i = files_count - 1; i >= 0; --i) {
        auto &f = proto::get_files(message, i);
        auto name = proto::get_name(f);
        if (seen_file_names.count(name)) {
            skipp_file_indices.insert(i);
        } else {
            seen_file_names.insert(name);
        }
    }

    // pass 2: do actual processing
    for (int i = 0; i < files_count; ++i) {
        auto &f = proto::get_files(message, i);
        auto name = proto::get_name(f);

        if (skipp_file_indices.count(i)) {
            LOG_DEBUG(log, "skipping outdated {}-th file '{}'", i, name);
            continue;
        }

        auto is_deleted = proto::get_deleted(f);
        auto is_invalid = proto::get_invalid(f);
        auto blocks_count = proto::get_blocks_size(f);
        auto is_downloadable = !is_deleted && !is_invalid;
        if (!is_downloadable && blocks_count) {
            LOG_WARN(log, "file {} should not have blocks", name);
            return make_error_code(error_code_t::unexpected_blocks);
        }
        auto sequence = proto::get_sequence(f);
        if (sequence <= prev_sequence) {
            LOG_DEBUG(log, "file '{}' has sequence {}, it is already known {}, try to ignore", name, sequence,
                      prev_sequence);
            auto prev_file = prev_files.by_name(name);
            if (!prev_file) {
                LOG_WARN(log, "no file '{}' has been previously seen, corrupted sequence?", name);
                return make_error_code(error_code_t::invalid_sequence);
            }
            if (!prev_file->identical_to(f)) {
                LOG_WARN(log, "no file '{}' is not identical previously seen, corrupted sequence?", name);
                return make_error_code(error_code_t::invalid_sequence);
            }
            continue;
        }

        if (sequence <= 0) {
            LOG_WARN(log, "file '{}' has wrong sequence", name, sequence);
            return make_error_code(error_code_t::invalid_sequence);
        }
        auto file_size = proto::get_size(f);
        auto size_by_blocks = std::int64_t(0);
        for (int j = 0; j < blocks_count; ++j) {
            auto &b = proto::get_blocks(f, j);
            size_by_blocks += proto::get_size(b);
            auto hash = proto::get_hash(b);
            auto strict_hash = block_info_t::make_strict_hash(hash);
            auto h = strict_hash.get_hash();
            if (!blocks.by_hash(h)) {
                auto from = reinterpret_cast<const char *>(h.data());
                auto to = from + h.size();
                auto hash_str = std::pmr::string(from, to, allocator);
                auto result = unique_new_blocks.emplace(hash_str);
                if (result.second) {
                    new_blocks.emplace_back(b);
                }
            }
        }
        if (is_downloadable && file_size != size_by_blocks) {
            return make_error_code(error_code_t::mismatch_file_size);
        }
        auto &version = proto::get_version(f);
        if (!proto::get_counters_size(version)) {
            return make_error_code(error_code_t::missing_version);
        }

        files.emplace_back(f);
    }

    return construct(sequencer, fi, device_id, std::move(files), std::move(new_blocks));
}

auto update_folder_t::create(const cluster_t &cluster, sequencer_t &sequencer, const model::device_t &source,
                             const proto::IndexBase &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, sequencer, source, message);
}
