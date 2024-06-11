// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "cluster_update.h"
#include "../modify/remove_blocks.h"
#include "../modify/remove_files.h"
#include "../modify/remove_folder_infos.h"
#include "../modify/remove_unknown_folders.h"
#include "../modify/share_folder.h"
#include "model/cluster.h"
#include "model/diff/aggregate.h"
#include "model/diff/cluster_visitor.h"
#include "utils/format.hpp"
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::peer;

auto cluster_update_t::create(const cluster_t &cluster, const device_t &source, const message_t &message) noexcept
    -> outcome::result<cluster_diff_ptr_t> {
    auto ptr = cluster_diff_ptr_t();
    auto log = get_log();

    auto &known_unknowns = cluster.get_unknown_folders();
    unknown_folders_t unknown;
    modified_folders_t updated;
    modified_folders_t reset;
    modified_folders_t remote;
    file_infos_map_t removed_files;
    keys_t removed_folders;
    keys_t removed_unknown_folders;
    keys_t confirmed_unknown_folders;
    keys_t confirmed_folders;
    keys_t removed_blocks;
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    keys_t reshared_folders;

    auto remove_blocks = [&](const model::file_info_ptr_t &fi) noexcept {
        auto &blocks = fi->get_blocks();
        for (auto &block : blocks) {
            auto predicate = [&](const model::file_block_t fb) -> bool {
                auto &same_blocks = fb.block()->get_file_blocks();
                auto is_deleted = [&](const model::file_block_t &b) -> bool {
                    return (bool)removed_files.get(b.file()->get_name());
                };
                return std::none_of(begin(same_blocks), end(same_blocks), is_deleted);
            };
            auto &file_blocks = block->get_file_blocks();
            bool remove = std::all_of(file_blocks.begin(), file_blocks.end(), predicate);
            if (remove) {
                removed_blocks.emplace(std::string(block->get_key()));
            }
        }
    };

    auto remove_folder = [&](const model::folder_info_t& folder_info) noexcept {
        removed_folders.emplace(folder_info.get_key());
        auto &files = folder_info.get_file_infos();
        for (auto it : files) {
            removed_files.put(it.item);
        }
        for (auto it : files) {
            remove_blocks(it.item);
        }
    };

    for (int i = 0; i < message.folders_size(); ++i) {
        auto &f = message.folders(i);
        auto folder = folders.by_id(f.id());
        LOG_TRACE(log, "cluster_update_t, folder = '{}'", f.label());
        if (!folder) {
            for (int i = 0; i < f.devices_size(); ++i) {
                auto &d = f.devices(i);
                if (d.id() == source.device_id().get_sha256()) {
                    for (auto &uf : known_unknowns) {
                        auto match = uf->device_id() == source.device_id() && uf->get_id() == f.id();
                        if (match) {
                            confirmed_unknown_folders.emplace(std::string(uf->get_key()));
                            bool actual = uf->get_index() == d.index_id() && uf->get_max_sequence() == d.max_sequence();
                            if (!actual) {
                                removed_unknown_folders.emplace(std::string(uf->get_key()));
                                unknown.emplace_back(f);
                                LOG_TRACE(log, "cluster_update_t, unknown folder = {}", f.label());
                            }
                        }
                        goto NEXT_FOLDER;
                    }
                    unknown.emplace_back(f);
                    LOG_TRACE(log, "cluster_update_t, unknown folder = {}", f.label());
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
            auto device_opt = model::device_id_t::from_sha256(device_sha);
            if (!device_opt) {
                auto device_hex = spdlog::to_hex(device_sha.begin(), device_sha.end());
                LOG_WARN(log, "cluster_update_t, malformed device id: {}", device_hex);
                continue;
            }
            auto &device_id = *device_opt;
            LOG_TRACE(log, "cluster_update_t, shared with device = '{}', index = {}, max seq. = {}", device_id,
                      d.index_id(), d.max_sequence());

            if (!device) {
                LOG_TRACE(log, "cluster_update_t, unknown device, ignoring");
                continue;
            }
            if (*device != source) {
                auto info = update_info_t{f.id(), d};
                remote.emplace_back(std::move(info));
                LOG_TRACE(log, "cluster_update_t, remote folder = {}", f.label());
                continue;
            }

            auto &folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device(*device);
            if (!folder_info) {
                auto log = get_log();
                LOG_WARN(log, "unexpected folder '{}' with a peer '{}'", folder->get_label(), device->device_id());
                continue;
            }

            auto update_info = update_info_t{f.id(), d};
            if (d.index_id() != folder_info->get_index()) {
                reset.emplace_back(update_info);
                remove_folder(*folder_info);
            } else if (d.max_sequence() > folder_info->get_max_sequence()) {
                LOG_TRACE(log, "cluster_update_t, updated folder = {}", f.label());
                updated.emplace_back(update_info);
            }
            confirmed_folders.emplace(folder_info->get_key());
        }
    }

    for (auto &uf : known_unknowns) {
        if (uf->device_id() != source.device_id()) {
            continue;
        }
        if (confirmed_unknown_folders.contains(uf->get_key())) {
            continue;
        }
        removed_unknown_folders.emplace(std::string(uf->get_key()));
    }

    auto diffs = aggregate_t::diffs_t();

    for (auto it_f: folders) {
        auto& folder = it_f.item;
        auto folder_info = folder->get_folder_infos().by_device(source);
        if (folder_info) {
            if (!confirmed_folders.contains(folder_info->get_key())) {
                remove_folder(*folder_info);
                reshared_folders.emplace(folder->get_id());
            }
        }
    }

    if (removed_files.size()) {
        diffs.emplace_back(new modify::remove_files_t(source, removed_files));
    }
    if (removed_folders.size()) {
        diffs.emplace_back(new modify::remove_folder_infos_t(std::move(removed_folders)));
    }
    if (!removed_blocks.empty()) {
        diffs.emplace_back(new modify::remove_blocks_t(std::move(removed_blocks)));
    }
    if (!removed_unknown_folders.empty()) {
        diffs.emplace_back(new modify::remove_unknown_folders_t(std::move(removed_unknown_folders)));
    }
    if (reshared_folders.size()) {
        for (auto& it: reshared_folders) {
            diffs.emplace_back(new modify::share_folder_t(source.device_id().get_sha256(), it));
        }
    }

    auto inner_diff = cluster_diff_ptr_t();
    if (!diffs.empty()) {
        inner_diff.reset(new aggregate_t(std::move(diffs)));
    }

    ptr = new cluster_update_t(source, std::move(unknown), std::move(reset), updated, remote, std::move(inner_diff));
    return outcome::success(std::move(ptr));
}

cluster_update_t::cluster_update_t(const model::device_t &source, unknown_folders_t unknown_folders,
                                   modified_folders_t reset_folders_, modified_folders_t updated_folders_,
                                   modified_folders_t remote_folders_, cluster_diff_ptr_t inner_diff_) noexcept
    : source_peer(source), new_unknown_folders{std::move(unknown_folders)}, reset_folders{std::move(reset_folders_)},
      updated_folders{std::move(updated_folders_)}, remote_folders{std::move(remote_folders_)},
      inner_diff{std::move(inner_diff_)} {}

auto cluster_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applying cluster_update_t");
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    auto peer = cluster.get_devices().by_sha256(source_peer.device_id().get_sha256());

    if (inner_diff) {
        auto r = inner_diff->apply(cluster);
        if (r.has_error()) {
            return r;
        }
    }

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
        db::FolderInfo db_fi;
        db_fi.set_index_id(info.device.index_id());
        db_fi.set_max_sequence(info.device.max_sequence());
        auto opt = folder_info_t::create(cluster.next_uuid(), db_fi, peer, folder);
        if (!opt) {
            return opt.assume_error();
        }
        folder_infos.put(opt.assume_value());
    }

    auto &unknown = cluster.get_unknown_folders();

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

    auto &peer_remote_folders = peer->get_remote_folder_infos();
    peer_remote_folders.clear();
    for (auto &info : remote_folders) {
        auto folder = folders.by_id(info.folder_id);
        assert(folder);
        auto device = devices.by_sha256(info.device.id());
        assert(device);
        auto opt = remote_folder_info_t::create(info.device, device, folder);
        if (!opt) {
            return opt.assume_error();
        }
        peer_remote_folders.put(opt.assume_value());
    }

    return outcome::success();
}

auto cluster_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
