// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster_update.h"
#include "model/diff/modify/add_remote_folder_infos.h"
#include "model/diff/modify/add_pending_folders.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/remove_pending_folders.h"
#include "model/diff/modify/reset_folder_infos.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/orphaned_blocks.h"
#include "utils/format.hpp"
#include "utils/string_comparator.hpp"
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/spdlog.h>

using namespace syncspirit::model::diff::peer;

using keys_t = std::set<std::string, syncspirit::utils::string_comparator_t>;

auto cluster_update_t::create(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                              const message_t &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return cluster_diff_ptr_t{new cluster_update_t(cluster, sequencer, source, message)};
};

cluster_update_t::cluster_update_t(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                                   const message_t &message) noexcept
    : peer_id(source.device_id().get_sha256()) {
    LOG_DEBUG(log, "cluster_update_t, source = {}", source.device_id().get_short());

    auto &known_pending_folders = cluster.get_pending_folders();
    auto new_pending_folders = diff::modify::add_pending_folders_t::container_t{};
    auto remote_folders = diff::modify::add_remote_folder_infos_t::container_t{};
    folder_infos_map_t reset_folders;
    folder_infos_map_t removed_folders;
    folder_infos_map_t reshared_folders;
    keys_t removed_pending_folders;
    keys_t confirmed_pending_folders;
    keys_t confirmed_folders;
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    auto orphaned_blocks = orphaned_blocks_t{};
    auto folder_update_diff = diff::cluster_diff_ptr_t{};
    auto folder_update = (diff::cluster_diff_t *){nullptr};

    auto add_pending = [&](const proto::Folder &f, const proto::Device &d) noexcept {
        using item_t = decltype(new_pending_folders)::value_type;
        LOG_TRACE(log, "cluster_update_t, (add/update) pending folder = {}", f.label());
        db::PendingFolder db;
        auto fi = db.mutable_folder_info();
        auto db_f = db.mutable_folder();
        fi->set_index_id(d.index_id());
        fi->set_max_sequence(d.max_sequence());
        db_f->set_id(f.id());
        db_f->set_label(f.label());
        db_f->set_read_only(f.read_only());
        db_f->set_ignore_permissions(f.ignore_permissions());
        db_f->set_ignore_delete(f.ignore_delete());
        db_f->set_disable_temp_indexes(f.disable_temp_indexes());
        db_f->set_paused(f.paused());

        auto id = std::string(source.device_id().get_sha256());
        new_pending_folders.push_back(item_t{std::move(db), std::move(id), sequencer.next_uuid()});
    };

    for (int i = 0; i < message.folders_size(); ++i) {
        auto &f = message.folders(i);
        auto &folder_id = f.id();
        auto folder = folders.by_id(folder_id);
        LOG_TRACE(log, "cluster_update_t, folder label = '{}', id = '{}'", f.label(), folder_id);
        if (!folder) {
            for (int i = 0; i < f.devices_size(); ++i) {
                auto &d = f.devices(i);
                if (d.id() == source.device_id().get_sha256()) {
                    for (auto &it : known_pending_folders) {
                        auto &uf = it.item;
                        auto match = uf->device_id() == source.device_id() && uf->get_id() == f.id();
                        if (match) {
                            confirmed_pending_folders.emplace(std::string(uf->get_key()));
                            bool actual = uf->get_index() == d.index_id() && uf->get_max_sequence() == d.max_sequence();
                            if (!actual) {
                                removed_pending_folders.emplace(std::string(uf->get_key()));
                                add_pending(f, d);
                            }
                            goto NEXT_FOLDER;
                        }
                    }
                    add_pending(f, d);
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
            LOG_TRACE(log, "cluster_update_t, shared with device = '{}', index = {:#x}, max seq. = {}", device_id,
                      d.index_id(), d.max_sequence());

            if (!device) {
                LOG_TRACE(log, "cluster_update_t, unknown device, ignoring");
                continue;
            }
            if (*device != source) {
                remote_folders.emplace_back(f.id(), d.index_id(), d.max_sequence());
                LOG_TRACE(log, "cluster_update_t, remote folder = {}, device = {}, max seq. = {}", f.label(),
                          device_id.get_short(), d.max_sequence());
                continue;
            }

            auto &folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device(*device);
            if (!folder_info) {
                LOG_TRACE(log, "cluster_update_t, adding pending folder {} non-shared with {}", f.label(),
                          device->device_id());
                add_pending(f, d);
                continue;
            }

            bool do_update = false;
            if (d.index_id() != folder_info->get_index()) {
                do_update = true;
                LOG_TRACE(log, "cluster_update_t, reseting folder: {}, new index = {:#x}, max_seq = {}", f.label(),
                          d.index_id(), d.max_sequence());
                reset_folders.put(folder_info);
            } else if (d.max_sequence() > folder_info->get_max_sequence()) {
                LOG_TRACE(log, "cluster_update_t, updating folder = {}, index = {:#x}, max seq = {} -> {}",
                          folder->get_label(), folder_info->get_index(), folder_info->get_max_sequence(),
                          d.max_sequence());
            }
            if (do_update) {
                auto uuid = bu::uuid{};
                assign(uuid, folder_info->get_uuid());
                auto ptr = cluster_diff_ptr_t{};
                ptr = new modify::upsert_folder_info_t(uuid, peer_id, f.id(), d.index_id());
                if (folder_update) {
                    folder_update = folder_update->assign_sibling(ptr.get());
                } else {
                    folder_update_diff = ptr;
                    folder_update = ptr.get();
                }
            }
            confirmed_folders.emplace(folder_info->get_key());
        }
    }

    for (auto &it : known_pending_folders) {
        auto &uf = it.item;
        if (uf->device_id() != source.device_id()) {
            continue;
        }
        if (confirmed_pending_folders.contains(uf->get_key())) {
            continue;
        }
        if (uf->device_id() == source.device_id()) {
            removed_pending_folders.emplace(std::string(uf->get_key()));
        }
    }

    for (auto it_f : folders) {
        auto &folder = it_f.item;
        auto folder_info = folder->get_folder_infos().by_device(source);
        if (folder_info) {
            if (!confirmed_folders.contains(folder_info->get_key())) {
                removed_folders.put(folder_info);
                reshared_folders.put(folder_info);
            }
        }
    }

    auto current = (cluster_diff_t *){nullptr};
    if (reset_folders.size()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::reset_folder_infos_t(std::move(reset_folders), &orphaned_blocks);
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (folder_update_diff) { // must be applied after folders reset
        auto &diff = folder_update_diff;
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (removed_folders.size()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_folder_infos_t(std::move(removed_folders), &orphaned_blocks);
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    auto removed_blocks = orphaned_blocks.deduce();
    if (!removed_blocks.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_blocks_t(std::move(removed_blocks));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (!removed_pending_folders.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_pending_folders_t(std::move(removed_pending_folders));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (reshared_folders.size()) {
        for (auto &it : reshared_folders) {
            auto &f = *it.item;
            auto uuid = bu::uuid{};
            assign(uuid, f.get_uuid());
            auto diff = cluster_diff_ptr_t{};
            diff = new modify::upsert_folder_info_t(uuid, peer_id, f.get_folder()->get_id(), 0);
            current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
        }
    }
    if (!new_pending_folders.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::add_pending_folders_t(std::move(new_pending_folders));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (!remote_folders.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::add_remote_folder_infos_t(source, std::move(remote_folders));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
}

auto cluster_update_t::apply_impl(cluster_t &cluster, apply_controller_t &controller) const noexcept
    -> outcome::result<void> {
    LOG_TRACE(log, "applying cluster_update_t (self)");
    auto peer = cluster.get_devices().by_sha256(peer_id);
    peer->get_remote_folder_infos().clear();

    LOG_TRACE(log, "applying cluster_update_t (children)");
    auto r = applicator_t::apply_child(cluster, controller);
    if (!r) {
        return r;
    }

    return applicator_t::apply_sibling(cluster, controller);
}

auto cluster_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
