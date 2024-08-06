// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "diff-builder.h"
#include "model/messages.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/add_unknown_device.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/finish_file_ack.h"
#include "model/diff/modify/local_update.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_ignored_device.h"
#include "model/diff/modify/remove_unknown_device.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/contact/peer_state.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

#include <algorithm>

using namespace syncspirit::test;
using namespace syncspirit::model;

cluster_configurer_t::cluster_configurer_t(diff_builder_t &builder_, std::string_view peer_sha256_) noexcept
    : builder{builder_}, peer_sha256{peer_sha256_} {}

cluster_configurer_t &&cluster_configurer_t::add(std::string_view sha256, std::string_view folder_id, uint64_t index,
                                                 int64_t max_sequence) noexcept {
    auto folder = cc.add_folders();
    folder->set_id(std::string(folder_id));
    auto device = folder->add_devices();
    device->set_id(std::string(sha256));
    device->set_index_id(index);
    device->set_max_sequence(max_sequence);
    return std::move(*this);
}

diff_builder_t &cluster_configurer_t::finish() noexcept {
    auto &cluster = builder.cluster;
    auto peer = builder.cluster.get_devices().by_sha256(peer_sha256);
    auto diff = diff::peer::cluster_update_t::create(cluster, *peer, cc);
    assert(diff.has_value());
    builder.assign(diff.value().get());
    return builder;
}

index_maker_t::index_maker_t(diff_builder_t &builder_, std::string_view sha256, std::string_view folder_id) noexcept
    : builder{builder_}, peer_sha256{sha256} {
    index.set_folder(std::string(folder_id));
}

index_maker_t &&index_maker_t::add(const proto::FileInfo &file) noexcept {
    *index.add_files() = file;
    return std::move(*this);
}

diff_builder_t &index_maker_t::finish() noexcept {
    auto &cluster = builder.cluster;
    auto peer = builder.cluster.get_devices().by_sha256(peer_sha256);
    auto diff = diff::peer::update_folder_t::create(cluster, *peer, index);
    assert(diff.has_value());
    builder.assign(diff.value().get());
    return builder;
}

diff_builder_t::diff_builder_t(model::cluster_t &cluster_) noexcept : cluster{cluster_} {}

diff_builder_t &diff_builder_t::apply(rotor::supervisor_t &sup) noexcept {
    auto has_diffs = [&]() -> bool { return cluster_diff || contact_diff || block_diff; };
    assert(has_diffs());

    auto &addr = sup.get_address();
    if (cluster_diff) {
        sup.send<model::payload::model_update_t>(addr, std::move(cluster_diff), nullptr);
    }
    if (contact_diff) {
        sup.send<model::payload::contact_update_t>(addr, std::move(contact_diff), nullptr);
    }
    if (block_diff) {
        sup.send<model::payload::block_update_t>(addr, std::move(block_diff), nullptr);
    }
    sup.do_process();

    return *this;
}

auto diff_builder_t::apply() noexcept -> outcome::result<void> {
    auto r = outcome::result<void>(outcome::success());
    bool do_try = true;
    while(do_try) {
        do_try = false;
        if (r && cluster_diff) {
            r = cluster_diff->apply(cluster);
            cluster_diff.reset();
            do_try = true;
        }
        if (r && contact_diff) {
            r = contact_diff->apply(cluster);
            contact_diff.reset();
            do_try = true;
        }
        if (r && block_diff) {
            r = block_diff->apply(cluster);
            block_diff.reset();
            do_try = true;
        }
    }
    return r;
}

diff_builder_t &diff_builder_t::create_folder(std::string_view id, std::string_view path,
                                              std::string_view label) noexcept {
    db::Folder db_folder;
    db_folder.set_id(std::string(id));
    db_folder.set_label(std::string(label));
    db_folder.set_path(std::string(path));
    return assign(new diff::modify::create_folder_t(db_folder));
}

diff_builder_t &diff_builder_t::update_peer(const model::device_id_t &device, std::string_view name,
                                            std::string_view cert_name, bool auto_accept) noexcept {
    db::Device db_device;
    db_device.set_name(std::string(name));
    db_device.set_cert_name(std::string(cert_name));
    db_device.set_auto_accept(auto_accept);

    return assign(new diff::modify::update_peer_t(db_device, device, cluster));
}

cluster_configurer_t diff_builder_t::configure_cluster(std::string_view sha256) noexcept {
    return cluster_configurer_t(*this, sha256);
}

index_maker_t diff_builder_t::make_index(std::string_view sha256, std::string_view folder_id) noexcept {
    return index_maker_t(*this, sha256, folder_id);
}

diff_builder_t &diff_builder_t::share_folder(std::string_view sha256, std::string_view folder_id) noexcept {
    return assign(new diff::modify::share_folder_t(sha256, folder_id));
}

diff_builder_t &diff_builder_t::unshare_folder(model::folder_info_t &fi) noexcept {
    return assign(new diff::modify::unshare_folder_t(cluster, fi));
}

diff_builder_t &diff_builder_t::clone_file(const model::file_info_t &source) noexcept {
    return assign(new diff::modify::clone_file_t(source));
}

diff_builder_t &diff_builder_t::finish_file(const model::file_info_t &source) noexcept {
    return assign(new diff::modify::finish_file_t(source));
}

diff_builder_t &diff_builder_t::finish_file_ack(const model::file_info_t &source) noexcept {
    return assign(new diff::modify::finish_file_ack_t(source));
}

diff_builder_t &diff_builder_t::local_update(std::string_view folder_id, const proto::FileInfo &file_) noexcept {
    return assign(new diff::modify::local_update_t(cluster, folder_id, file_));
}

diff_builder_t &diff_builder_t::remove_peer(const model::device_t &peer) noexcept {
    return assign(new diff::modify::remove_peer_t(cluster, peer));
    return *this;
}

diff_builder_t &diff_builder_t::update_state(const model::device_t &peer, const r::address_ptr_t &peer_addr,
                                             model::device_state_t state) noexcept {
    return assign(new model::diff::contact::peer_state_t(cluster, peer.device_id().get_sha256(), peer_addr, state));
}

diff_builder_t &diff_builder_t::update_contact(const model::device_id_t &device,
                                               const utils::uri_container_t &uris) noexcept {
    return assign(new model::diff::contact::update_contact_t(cluster, device, uris));
}

diff_builder_t &diff_builder_t::append_block(const model::file_info_t &target, size_t block_index, std::string data,
                                             dispose_callback_t callback) noexcept {
    return assign(new diff::modify::append_block_t(target, block_index, std::move(data), std::move(callback)));
}

diff_builder_t &diff_builder_t::clone_block(const model::file_block_t &file_block,
                                            dispose_callback_t callback) noexcept {
    return assign(new diff::modify::clone_block_t(file_block, std::move(callback)));
}

diff_builder_t &diff_builder_t::ack_block(const model::diff::modify::block_transaction_t &diff) noexcept {
    return assign(new diff::modify::block_ack_t(diff));
    return *this;
}

diff_builder_t &diff_builder_t::add_ignored_device(const model::device_id_t &device,
                                                   db::SomeDevice db_device) noexcept {
    return assign(new diff::modify::add_ignored_device_t(cluster, device, db_device));
}

diff_builder_t &diff_builder_t::add_unknown_device(const model::device_id_t &device,
                                                   db::SomeDevice db_device) noexcept {
    return assign(new diff::modify::add_unknown_device_t(device, db_device));
}

diff_builder_t &diff_builder_t::remove_ignored_device(const model::ignored_device_t &device) noexcept {
    return assign(new diff::modify::remove_ignored_device_t(device));
}

diff_builder_t &diff_builder_t::remove_unknown_device(const model::unknown_device_t &device) noexcept {
    return assign(new diff::modify::remove_unknown_device_t(device));
}

template <typename Holder, typename Diff> static void generic_assign(Holder *holder, Diff *diff) noexcept {
    if (!(*holder)) {
        holder->reset(diff);
    }
    else {
        auto h = *holder;
        while (h && h->sibling) {
            h = h->sibling;
        }
        h->assign_sibling(diff);
    }
}

diff_builder_t &diff_builder_t::assign(model::diff::cluster_diff_t *diff) noexcept {
    generic_assign(&cluster_diff, diff);
    return *this;
}

diff_builder_t &diff_builder_t::assign(model::diff::contact_diff_t *diff) noexcept {
    generic_assign(&contact_diff, diff);
    return *this;
}

diff_builder_t &diff_builder_t::assign(model::diff::block_diff_t *diff) noexcept {
    generic_assign(&block_diff, diff);
    return *this;
}
