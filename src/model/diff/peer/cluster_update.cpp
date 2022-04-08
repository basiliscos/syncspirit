// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "cluster_update.h"
#include "cluster_remove.h"
#include "model/cluster.h"
#include "model/diff/aggregate.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/string_map.hpp"
#include "model/misc/error_code.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::peer;

auto cluster_update_t::create(const cluster_t &cluster, const device_t &source, const message_t &message) noexcept
    -> outcome::result<cluster_diff_ptr_t> {
    auto ptr = cluster_diff_ptr_t();

    auto &known_unknowns = cluster.get_unknown_folders();
    unknown_folders_t unknown;
    modified_folders_t updated;
    modified_folders_t reset;
    keys_t removed_folders;
    keys_t removed_files_final;
    keys_t removed_unknown_folders;
    string_map removed_files;
    keys_t removed_blocks;
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();

    auto remove_blocks = [&](const model::file_info_ptr_t &fi) noexcept {
        auto &blocks = fi->get_blocks();
        for (auto &block : blocks) {
            auto predicate = [&](const model::file_block_t fb) -> bool {
                auto r = removed_files.get(fb.file()->get_key());
                return !r.empty();
            };
            auto &file_blocks = block->get_file_blocks();
            bool remove = std::all_of(file_blocks.begin(), file_blocks.end(), predicate);
            if (remove) {
                removed_blocks.emplace(std::string(block->get_hash()));
            }
        }
    };

    for (int i = 0; i < message.folders_size(); ++i) {
        auto &f = message.folders(i);
        auto folder = folders.by_id(f.id());
        if (!folder) {
            for (int i = 0; i < f.devices_size(); ++i) {
                auto &d = f.devices(i);
                if (d.id() == source.device_id().get_sha256()) {
                    for (auto &uf : known_unknowns) {
                        auto match = uf->device_id() == source.device_id() && uf->get_id() == f.id();
                        if (match) {
                            bool actual = uf->get_index() == d.index_id() && uf->get_max_sequence() == d.max_sequence();
                            if (!actual) {
                                removed_unknown_folders.emplace(std::string(uf->get_key()));
                                unknown.emplace_back(f);
                            }
                        }
                        goto NEXT_FOLDER;
                    }
                    unknown.emplace_back(f);
                    goto NEXT_FOLDER;
                }
            }
        NEXT_FOLDER:
            continue;
        }

        for (int j = 0; j < f.devices_size(); ++j) {
            auto &d = f.devices(j);
            auto device_sha = d.id();
            auto device = devices.by_sha256(device_sha);
            if (!device) {
                continue;
            }
            if (device != &source) {
                continue;
            }

            auto &folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device(device);
            if (!folder_info) {
                auto log = get_log();
                LOG_WARN(log, "folder {} was not shared with a peer {}", folder->get_label(), device->device_id());
                return make_error_code(error_code_t::folder_is_not_shared);
            }

            auto update_info = update_info_t{f.id(), d};
            if (d.index_id() != folder_info->get_index()) {
                reset.emplace_back(update_info);
                removed_folders.emplace(folder_info->get_key());
                auto &files = folder_info->get_file_infos();
                for (auto it : files) {
                    auto key = std::string(it.item->get_key());
                    removed_files_final.emplace(key);
                    removed_files.put(key);
                }
                for (auto it : files) {
                    remove_blocks(it.item);
                }
            } else if (d.max_sequence() > folder_info->get_max_sequence()) {
                updated.emplace_back(update_info);
            }
        }
    }

    ptr = new cluster_update_t(source, std::move(unknown), std::move(reset), std::move(updated), removed_blocks,
                               std::move(removed_unknown_folders));
    bool wrap = !removed_blocks.empty() || !removed_folders.empty();
    if (wrap) {
        keys_t updated_folders;
        for (auto &info : updated) {
            updated_folders.emplace(info.folder_id);
        }

        auto remove = cluster_diff_ptr_t(new cluster_remove_t(
            source.device_id().get_sha256(), std::move(updated_folders), std::move(removed_folders),
            std::move(removed_files_final), std::move(removed_blocks)));
        auto diffs = aggregate_t::diffs_t{std::move(ptr), std::move(remove)};
        auto container = cluster_diff_ptr_t(new aggregate_t(std::move(diffs)));
        return outcome::success(std::move(container));
    }
    return outcome::success(std::move(ptr));
}

cluster_update_t::cluster_update_t(const model::device_t &source, unknown_folders_t unknown_folders,
                                   modified_folders_t reset_folders_, modified_folders_t updated_folders_,
                                   keys_t removed_blocks_, keys_t removed_unknown_folders_) noexcept
    : source_peer(source), new_unknown_folders{std::move(unknown_folders)}, reset_folders{std::move(reset_folders_)},
      updated_folders{std::move(updated_folders_)}, removed_blocks{removed_blocks_}, removed_unknown_folders{std::move(
                                                                                         removed_unknown_folders_)} {}

auto cluster_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applyging cluster_update_t");
    auto &folders = cluster.get_folders();

    for (auto &info : updated_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto folder_info = folder->get_folder_infos().by_device_id(info.device.id());
        auto max_seq = info.device.max_sequence();
        folder_info->set_max_sequence(max_seq);
        spdlog::trace("cluster_update_t::apply folder = {}, index = {:#x}, max seq = {} -> {}", folder->get_label(),
                      folder_info->get_index(), folder_info->get_max_sequence(), max_seq);
    }

    for (auto &info : reset_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto &folder_infos = folder->get_folder_infos();
        auto folder_info = folder_infos.by_device_id(info.device.id());
        folder_infos.remove(folder_info);
        db::FolderInfo db_fi;
        db_fi.set_index_id(info.device.index_id());
        db_fi.set_max_sequence(info.device.max_sequence());
        auto opt = folder_info_t::create(cluster.next_uuid(), db_fi, folder_info->get_device(), folder);
        if (!opt) {
            return opt.assume_error();
        }
        folder_infos.put(opt.assume_value());
    }

    auto &blocks = cluster.get_blocks();
    for (auto &id : removed_blocks) {
        auto block = blocks.get(id);
        blocks.remove(block);
    }

    auto &unknown = cluster.get_unknown_folders();
    for (auto &key : removed_unknown_folders) {
        for (auto it = unknown.begin(), prev = unknown.before_begin(); it != unknown.end(); prev = it, ++it) {
            if ((**it).get_key() == key) {
                unknown.erase_after(prev);
                break;
            }
        }
    }

    for (auto &folder : new_unknown_folders) {
        db::UnknownFolder db;
        auto fi = db.mutable_folder_info();
        for (int i = 0; i < folder.devices_size(); ++i) {
            auto &d = folder.devices(i);
            if (d.id() == source_peer.device_id().get_sha256()) {
                fi->set_index_id(d.index_id());
                fi->set_max_sequence(d.max_sequence());
                break;
            }
        }
        auto f = db.mutable_folder();
        f->set_id(folder.id());
        f->set_label(folder.label());
        f->set_read_only(folder.read_only());
        f->set_ignore_permissions(folder.ignore_permissions());
        f->set_ignore_delete(folder.ignore_delete());
        f->set_disable_temp_indexes(folder.disable_temp_indexes());
        f->set_paused(folder.paused());
        auto opt = unknown_folder_t::create(cluster.next_uuid(), db, source_peer.device_id());
        if (!opt) {
            return opt.assume_error();
        }
        unknown.emplace_front(std::move(opt.value()));
    }

    return outcome::success();
}

auto cluster_update_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    return visitor(*this);
}
