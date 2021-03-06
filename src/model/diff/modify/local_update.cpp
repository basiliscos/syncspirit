// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "local_update.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/string_map.hpp"

using namespace syncspirit::model::diff::modify;

local_update_t::local_update_t(const file_info_t &file, db::FileInfo current_, blocks_t current_blocks_) noexcept
    : current{std::move(current_)}, current_blocks{std::move(current_blocks_)} {

    folder_id = file.get_folder_info()->get_folder()->get_id();
    file_name = file.get_name();

    prev = file.as_db(false);

    auto &blocks = file.get_blocks();
    if (blocks.size() != current_blocks.size()) {
        blocks_updated = true;
    } else {
        for (size_t i = 0; i < blocks.size(); ++i) {
            bool mismatch = !blocks[i] || (blocks[i]->get_size() != current_blocks[i].size()) ||
                            (blocks[i]->get_weak_hash() != current_blocks[i].weak_hash()) ||
                            (blocks[i]->get_hash() != current_blocks[i].hash());
            if (mismatch) {
                blocks_updated = true;
                break;
            }
        }
    }

    if (blocks_updated) {
        /* save prev blocks */
        prev_blocks.reserve(blocks.size());
        for (const auto &b : blocks) {
            if (b) {
                prev_blocks.push_back(b->as_bep(0));
            }
        }

        auto tmp_blocks = string_map{};
        for (size_t i = 0; i < current_blocks.size(); ++i) {
            auto &b = current_blocks[i];
            tmp_blocks.put(b.hash());
        };
        for (const auto &b : blocks) {
            if (b && tmp_blocks.get(b->get_hash()).empty()) {
                removed_blocks.insert(std::string(b->get_hash()));
            }
        };
    }
}

auto local_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto device = cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(device);
    auto file = folder_info->get_file_infos().by_name(file_name);
    auto &blocks_map = cluster.get_blocks();
    block_infos_map_t tmp_blocks;
    auto &blocks = file->get_blocks();

    if (blocks_updated) {
        for (auto &b : blocks) {
            if (b) {
                tmp_blocks.put(b);
            }
        }
    }

    auto r = file->fields_update(current);
    if (!r) {
        return r.assume_error();
    }

    auto seq = folder_info->get_max_sequence() + 1;
    folder_info->set_max_sequence(seq);
    file->set_sequence(seq);

    if (blocks_updated) {
        file->remove_blocks();

        for (size_t i = 0; i < current_blocks.size(); ++i) {
            auto &b = current_blocks[i];
            auto &hash = b.hash();
            auto block = blocks_map.get(hash);
            if (!block) {
                block = tmp_blocks.get(hash);
                if (block) {
                    blocks_map.put(block);
                }
            }
            if (!block) {
                auto opt = block_info_t::create(b);
                if (!opt) {
                    return opt.assume_error();
                }
                block = std::move(opt.assume_value());
                blocks_map.put(block);
            }
            file->assign_block(block, i);
        }

        for (auto &hash : removed_blocks) {
            auto b = blocks_map.get(hash);
            if (b->get_file_blocks().empty()) {
                blocks_map.remove(b);
            }
        }
    }

    return outcome::success();
}

auto local_update_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t");
    return visitor(*this);
}
