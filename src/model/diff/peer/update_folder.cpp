// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "update_folder.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff::peer;

update_folder_t::update_folder_t(std::string_view folder_id_, std::string_view peer_id_, files_t files_,
                                 blocks_t blocks_) noexcept
    : folder_id{std::string(folder_id_)}, peer_id{std::string(peer_id_)}, files{std::move(files_)}, blocks{std::move(
                                                                                                        blocks_)} {}

auto update_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(peer_id);

    auto &bm = cluster.get_blocks();
    auto blocks_map = block_infos_map_t();
    for (const auto &b : blocks) {
        auto opt = block_info_t::create(b);
        if (!opt) {
            return opt.assume_error();
        }
        auto block = std::move(opt.assume_value());
        blocks_map.put(block);
    }

    auto max_seq = folder_info->get_max_sequence();
    auto files_map = file_infos_map_t();
    auto &fm = folder_info->get_file_infos();
    for (const auto &f : files) {
        auto file = file_info_ptr_t{};
        uuid_t file_uuid;
        auto prev_file = fm.by_name(f.name());
        if (prev_file) {
            assign(file_uuid, prev_file->get_uuid());
        } else {
            file_uuid = cluster.next_uuid();
        }
        auto opt = file_info_t::create(file_uuid, f, folder_info);
        if (!opt) {
            return opt.assume_error();
        }
        file = std::move(opt.assume_value());
        files_map.put(file);
        max_seq = std::max(max_seq, file->get_sequence());

        for (int i = 0; i < f.blocks_size(); ++i) {
            auto &b = f.blocks(i);
            auto block = blocks_map.get(b.hash());
            if (!block) {
                block = bm.get(b.hash());
            }
            if (!block) {
                auto opt = block_info_t::create(b);
                if (!opt) {
                    return opt.assume_error();
                }
                block = std::move(opt.value());
            }
            file->assign_block(block, (size_t)i);
        }
        if (!file->check_consistency()) {
            LOG_ERROR(log, "inconsitency detected for the file {} at folder {}", file->get_name(), folder->get_label());
            return make_error_code(error_code_t::inconsistent_file);
        }
    }

    // all ok, commit
    for (auto &it : blocks_map) {
        bm.put(it.item);
    }
    for (auto &it : files_map) {
        folder_info->add(it.item);
    }
    if (max_seq) {
        folder_info->set_max_sequence(max_seq);
    }

    return outcome::success();
}

auto update_folder_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting update_folder_t", folder_id);
    return visitor(*this);
}

using diff_t = diff::cluster_diff_ptr_t;

template <typename T>
static auto instantiate(const cluster_t &cluster, const device_t &source, const T &message) noexcept
    -> outcome::result<diff_t> {
    auto folder = cluster.get_folders().by_id(message.folder());
    if (!folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto device_id = source.device_id().get_sha256();
    auto fi = folder->get_folder_infos().by_device_id(device_id);
    if (!fi) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    auto max_seq = fi->get_max_sequence();
    auto &blocks = cluster.get_blocks();
    update_folder_t::files_t files;
    update_folder_t::blocks_t new_blocks;
    files.reserve(static_cast<size_t>(message.files_size()));
    for (int i = 0; i < message.files_size(); ++i) {
        auto &f = message.files(i);
        if (f.deleted() && f.blocks_size()) {
            auto log = update_folder_t::get_log();
            LOG_WARN(log, "file {}, should not have blocks", f.name());
            return make_error_code(error_code_t::unexpected_blocks);
        }
        for (int j = 0; j < f.blocks_size(); ++j) {
            auto &b = f.blocks(j);
            if (!blocks.get(b.hash())) {
                new_blocks.emplace_back(std::move(b));
            }
        }
        files.emplace_back(std::move(message.files(i)));
        max_seq = std::max(max_seq, f.sequence());
    }

    if ((max_seq <= fi->get_max_sequence()) && message.files_size()) {
        return make_error_code(error_code_t::no_progress);
    }

    auto diff = diff_t(new update_folder_t(message.folder(), device_id, std::move(files), std::move(new_blocks)));
    return outcome::success(std::move(diff));
}

auto update_folder_t::create(const cluster_t &cluster, const model::device_t &source,
                             const proto::Index &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, source, message);
}

auto update_folder_t::create(const cluster_t &cluster, const model::device_t &source,
                             const proto::IndexUpdate &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, source, message);
}
