// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "cluster_update.h"
#include "model/diff/modify/add_remote_folder_infos.h"
#include "model/diff/modify/add_pending_folders.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_pending_folders.h"
#include "model/diff/modify/reset_folder_infos.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/cluster_visitor.h"
#include "model/cluster.h"
#include "model/misc/orphaned_blocks.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "utils/uri.h"
#include <spdlog/fmt/bin_to_hex.h>
#include <spdlog/spdlog.h>

using namespace syncspirit;
using namespace syncspirit::model::diff::peer;

using keys_t = syncspirit::model::diff::modify::generic_remove_t::unique_keys_t;
using keys_view_t = std::set<utils::bytes_view_t, utils::bytes_comparator_t>;

auto cluster_update_t::create(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                              const message_t &message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    auto diff = cluster_diff_ptr_t();
    auto ptr = new cluster_update_t(cluster, sequencer, source, message);
    diff.reset(ptr);
    if (ptr->ec) {
        return ptr->ec;
    }
    return diff;
};

cluster_update_t::cluster_update_t(const cluster_t &cluster, sequencer_t &sequencer, const device_t &source,
                                   const message_t &message) noexcept {
    struct introduced_device_t {
        db::Device device;
        model::device_id_t device_id;
    };
    using introduced_devices_t = std::vector<introduced_device_t>;
    struct inserted_folder_info_t {
        std::string_view folder_id;
        std::uint64_t new_index_id;
        model::device_id_t device_id;
    };
    using inserted_folder_infos_t = std::vector<inserted_folder_info_t>;

    auto sha256 = source.device_id().get_sha256();
    peer_id = sha256;
    LOG_DEBUG(log, "cluster_update_t, source = {}", source.device_id().get_short());

    auto &known_pending_folders = cluster.get_pending_folders();
    auto new_pending_folders = diff::modify::add_pending_folders_t::container_t{};
    auto remote_folders = diff::modify::add_remote_folder_infos_t::container_t{};
    uuid_folder_infos_map_t reset_folders;
    uuid_folder_infos_map_t removed_folders;
    folder_infos_map_t reshared_folders;
    keys_t removed_pending_folders;
    keys_t confirmed_pending_folders;
    keys_view_t confirmed_folders;
    keys_view_t processed_introduced_devices;
    keys_view_t seen_devices;
    auto &folders = cluster.get_folders();
    auto &devices = cluster.get_devices();
    auto orphaned_blocks = orphaned_blocks_t{};
    auto folder_update_diff = diff::cluster_diff_ptr_t{};
    auto folder_update = (diff::cluster_diff_t *){nullptr};
    auto introduced_devices = introduced_devices_t();
    auto inserted_folder_infos = inserted_folder_infos_t();

    auto add_pending = [&](const proto::Folder &f, const proto::Device &d) noexcept {
        auto folder_id = proto::get_id(f);
        for (auto &it : known_pending_folders) {
            auto &uf = it.item;
            auto match = uf->device_id().get_sha256() == sha256 && uf->get_id() == folder_id;
            if (match) {
                auto index_id = proto::get_index_id(d);
                auto key = uf->get_key();
                auto uf_key = utils::bytes_t(key.begin(), key.end());
                confirmed_pending_folders.emplace(uf_key);
                auto index_match = uf->get_index() == index_id;
                auto sequence_match = uf->get_max_sequence() == proto::get_max_sequence(d);
                bool actual = index_match && sequence_match;
                if (!actual) {
                    removed_pending_folders.emplace(std::move(uf_key));
                } else {
                    return;
                }
            }
        }

        using item_t = decltype(new_pending_folders)::value_type;
        auto label = proto::get_label(f);
        LOG_DEBUG(log, "cluster_update_t, (add/update) pending folder = {}", label);
        db::PendingFolder db;
        auto &db_fi = db::get_folder_info(db);
        auto &db_f = db::get_folder(db);
        db::set_index_id(db_fi, proto::get_index_id(d));
        db::set_max_sequence(db_fi, proto::get_max_sequence(d));
        db::set_id(db_f, proto::get_id(f));
        db::set_label(db_f, label);
        db::set_read_only(db_f, proto::get_read_only(f));
        db::set_ignore_permissions(db_f, proto::get_ignore_permissions(f));
        db::set_ignore_delete(db_f, proto::get_ignore_delete(f));
        db::set_disable_temp_indexes(db_f, proto::get_disable_temp_indexes(f));
        db::set_paused(db_f, proto::get_paused(f));

        auto id = utils::bytes_t(sha256.begin(), sha256.end());
        new_pending_folders.push_back(item_t{std::move(db), std::move(id), sequencer.next_uuid()});
    };

    auto add_upsert_folder_info = [&](modify::upsert_folder_info_t *diff) noexcept {
        auto ptr = cluster_diff_ptr_t(diff);
        if (folder_update) {
            folder_update = folder_update->assign_sibling(ptr.get());
        } else {
            folder_update_diff = ptr;
            folder_update = ptr.get();
        }
    };

    auto upsert_folder_info = [&](const model::folder_info_t &fi, std::uint64_t new_index_id) noexcept {
        add_upsert_folder_info(new modify::upsert_folder_info_t(fi, new_index_id));
    };

    auto upsert_folder = [&](std::string_view folder_id, const proto::Device &device, const device_id_t &device_id) {
        LOG_DEBUG(log, "cluster_update_t, going to share folder '{}' with introduced device '{}'", folder_id,
                  device_id);
        auto index_id = proto::get_index_id(device);
        inserted_folder_infos.emplace_back(inserted_folder_info_t(folder_id, index_id, device_id));
    };

    auto introduce_device = [&](std::string_view folder_id, const proto::Device &device,
                                const device_id_t &device_id) noexcept -> bool {
        auto device_name = proto::get_name(device);
        auto sha256 = proto::get_id(device);
        if (!processed_introduced_devices.count(sha256)) {
            processed_introduced_devices.emplace(sha256);
            seen_devices.emplace(sha256);
            auto db_peer = db::Device();
            auto addresses_count = proto::get_addresses_size(device);
            for (size_t i = 0; i < addresses_count; ++i) {
                auto address = proto::get_addresses(device, i);
                if (address != "dynamic") {
                    if (utils::is_parsable(address)) {
                        db::add_addresses(db_peer, address);
                    } else {
                        ec = make_error_code(utils::error_code_t::malformed_url);
                        return false;
                    }
                }
            }
            db::set_name(db_peer, device_name);
            db::set_compression(db_peer, proto::get_compression(device));
            db::set_introducer(db_peer, proto::get_introducer(device));
            db::set_skip_introduction_removals(db_peer, proto::get_skip_introduction_removals(device));
            introduced_devices.emplace_back(introduced_device_t{std::move(db_peer), device_id});
        }

        upsert_folder(folder_id, device, device_id);
        return true;
    };

    auto folders_count = proto::get_folders_size(message);
    for (size_t i = 0; i < folders_count; ++i) {
        auto &f = proto::get_folders(message, i);
        auto folder_id = proto::get_id(f);
        auto folder_label = proto::get_label(f);
        auto folder = folders.by_id(folder_id);
        LOG_DEBUG(log, "cluster_update_t, folder label = '{}', id = '{}'", folder_label, folder_id);
        auto devices_count = proto::get_devices_size(f);
        for (int j = 0; j < devices_count; ++j) {
            auto &d = proto::get_devices(f, j);
            auto device_sha = proto::get_id(d);
            auto device = devices.by_sha256(device_sha);
            auto device_opt = model::device_id_t::from_sha256(device_sha);
            if (!device_opt) {
                auto device_hex = spdlog::to_hex(device_sha.begin(), device_sha.end());
                LOG_WARN(log, "cluster_update_t, malformed device id: {}", device_hex);
                continue;
            }
            auto index_id = proto::get_index_id(d);
            auto &device_id = *device_opt;
            auto max_sequence = proto::get_max_sequence(d);
            LOG_DEBUG(log, "cluster_update_t, shared with device = '{}', index = {:#x}, max seq. = {}", device_id,
                      index_id, max_sequence);

            if (!device) {
                if (source.is_introducer()) {
                    if (folder && folder->is_shared_with(source)) {
                        if (introduce_device(folder_id, d, device_id)) {
                            continue;
                        } else {
                            return;
                        }
                    }
                }
                LOG_TRACE(log, "cluster_update_t, unknown device, ignoring");
                continue;
            }
            seen_devices.emplace(device_sha);

            auto folder_info = model::folder_info_ptr_t();
            if (folder && device) {
                folder_info = folder->get_folder_infos().by_device_id(device_sha);
            }

            if (device.get() == cluster.get_device()) {
                remote_folders.emplace_back(std::string(folder_id), index_id, max_sequence);
                LOG_DEBUG(log, "cluster_update_t, remote folder = {}, device = {}, max seq. = {}", folder_label,
                          device_id.get_short(), max_sequence);
                continue;
            }

            if (!folder_info) {
                if (device_sha == source.device_id().get_sha256()) {
                    LOG_DEBUG(log, "cluster_update_t, adding pending folder {} non-shared with {}", folder_label,
                              device->device_id());
                    add_pending(f, d);
                } else if (source.is_introducer()) {
                    upsert_folder(folder_id, d, device_id);
                }
                continue;
            } else {
                bool do_update = false;
                if (index_id != folder_info->get_index()) {
                    do_update = true;
                    LOG_DEBUG(log, "cluster_update_t, reseting folder: {}, new index = {:#x}, max_seq = {}", folder_label,
                              index_id, max_sequence);
                    reset_folders.emplace(folder_info->get_uuid(), folder_info.get());
                } else if (max_sequence > folder_info->get_max_sequence()) {
                    LOG_DEBUG(log, "cluster_update_t, updating folder = {}, index = {:#x}, max seq = {} -> {}",
                              folder->get_label(), folder_info->get_index(), folder_info->get_max_sequence(), max_sequence);
                }
                if (do_update) {
                    upsert_folder_info(*folder_info, index_id);
                }
                confirmed_folders.emplace(folder_info->get_key());
            }
        }
    }

    for (auto &it : known_pending_folders) {
        auto &uf = it.item;
        if (uf->device_id().get_sha256() != sha256) {
            continue;
        }
        if (confirmed_pending_folders.contains(uf->get_key())) {
            continue;
        }
        if (uf->device_id().get_sha256() == sha256) {
            auto key = uf->get_key();
            auto uf_key = utils::bytes_t(key.begin(), key.end());
            removed_pending_folders.emplace(std::move(uf_key));
        }
    }

    for (auto it_f : folders) {
        auto &folder = it_f.item;
        auto folder_info = folder->get_folder_infos().by_device(source);
        if (folder_info) {
            if (!confirmed_folders.contains(folder_info->get_key())) {
                removed_folders.emplace(folder_info->get_uuid(), folder_info.get());
                reshared_folders.put(folder_info);
            }
        }
    }

    keys_view_t removed_introduced_devices;
    if (!source.get_skip_introduction_removals()) {
        for (auto fit : cluster.get_folders()) {
            auto &folder_infos = fit.item->get_folder_infos();
            for (auto it : folder_infos) {
                auto &fi = *it.item;
                auto &i_device = fi.get_device()->device_id();
                if (fi.is_introduced_by(source.device_id())) {
                    auto remove_fi = !confirmed_folders.count(fi.get_key()) && !removed_folders.count(fi.get_uuid());
                    if (remove_fi) {
                        auto sha256 = i_device.get_sha256();
                        if (!seen_devices.count(sha256)) {
                            removed_introduced_devices.emplace(sha256);
                        } else {
                            removed_folders.emplace(fi.get_uuid(), &fi);
                            LOG_DEBUG(log, "unsharing folder '{}' with introduced device '{}'",
                                      fi.get_folder()->get_id(), i_device);
                        }
                    }
                }
            }
        }
    }

    auto current = (cluster_diff_t *){nullptr};
    auto update_current = [&](cluster_diff_t *diff) {
        current = current ? current->assign_sibling(diff) : assign_child(diff);
    };

    if (reset_folders.size()) {
        auto ptr = new modify::reset_folder_infos_t(std::move(reset_folders), &orphaned_blocks);
        update_current(ptr);
    }
    if (folder_update_diff) { // must be applied after folders reset
        auto &diff = folder_update_diff;
        update_current(folder_update_diff.get());
    }
    if (!removed_introduced_devices.empty()) {
        for (auto sha256 : removed_introduced_devices) {
            auto peer = devices.by_sha256(sha256);
            update_current(new modify::remove_peer_t(cluster, *peer));
            LOG_DEBUG(log, "removing introduced device '{}'", peer->device_id());
        }
    }
    if (removed_folders.size()) {
        auto ptr = new modify::remove_folder_infos_t(std::move(removed_folders), &orphaned_blocks);
        update_current(ptr);
    }
    auto removed_blocks = orphaned_blocks.deduce();
    if (!removed_blocks.empty()) {
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_blocks_t(std::move(removed_blocks));
        current = current ? current->assign_sibling(diff.get()) : assign_child(diff);
    }
    if (!removed_pending_folders.empty()) {
        auto ptr = new modify::remove_pending_folders_t(std::move(removed_pending_folders));
        update_current(ptr);
    }
    if (reshared_folders.size()) {
        for (auto &it : reshared_folders) {
            auto &f = *it.item;
            update_current(new modify::upsert_folder_info_t(f, 0));
        }
    }
    if (!new_pending_folders.empty()) {
        auto ptr = new modify::add_pending_folders_t(std::move(new_pending_folders));
        update_current(ptr);
    }
    if (!remote_folders.empty()) {
        auto ptr = new modify::add_remote_folder_infos_t(source, std::move(remote_folders));
        update_current(ptr);
    }
    for (auto &id : introduced_devices) {
        auto ptr = new diff::modify::update_peer_t(std::move(id.device), id.device_id, cluster);
        update_current(ptr);
    }
    for (auto &info : inserted_folder_infos) {
        auto ptr = new diff::modify::upsert_folder_info_t(sequencer.next_uuid(), info.device_id, source.device_id(),
                                                          info.folder_id, info.new_index_id);
        update_current(ptr);
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
