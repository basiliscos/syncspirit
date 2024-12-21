// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "diff-builder.h"
#include "model/messages.h"
#include "model/diff/advance/remote_copy.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_request.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/synchronization_finish.h"
#include "model/diff/local/synchronization_start.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/add_pending_device.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/mark_reachable.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_ignored_device.h"
#include "model/diff/modify/remove_pending_device.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/contact/update_contact.h"
#include "model/diff/contact/peer_state.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

using namespace syncspirit::test;
using namespace syncspirit::model;

cluster_configurer_t::cluster_configurer_t(diff_builder_t &builder_, std::string_view peer_sha256_) noexcept
    : builder{builder_}, peer_sha256{peer_sha256_}, folder{nullptr} {}

cluster_configurer_t &&cluster_configurer_t::add(std::string_view sha256, std::string_view folder_id, uint64_t index,
                                                 int64_t max_sequence) noexcept {
    folder = cc.add_folders();
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
    auto diff = diff::peer::cluster_update_t::create(cluster, *builder.sequencer, *peer, cc);
    assert(diff.has_value());
    builder.assign(diff.value().get());
    return builder;
}

index_maker_t::index_maker_t(diff_builder_t &builder_, std::string_view sha256, std::string_view folder_id) noexcept
    : builder{builder_}, peer_sha256{sha256} {
    index.set_folder(std::string(folder_id));
}

index_maker_t &&index_maker_t::add(const proto::FileInfo &file, const model::device_ptr_t &peer,
                                   bool add_version) noexcept {
    auto f = index.add_files();
    *f = file;
    if (add_version && f->version().counters_size() == 0) {
        auto v = f->mutable_version();
        auto c = v->add_counters();
        c->set_id(peer->as_uint());
        c->set_value(1);
    }
    return std::move(*this);
}

std::error_code index_maker_t::fail() noexcept {
    auto &cluster = builder.cluster;
    auto peer = builder.cluster.get_devices().by_sha256(peer_sha256);
    auto opt = diff::peer::update_folder_t::create(cluster, *builder.sequencer, *peer, index);
    return opt.error();
}

diff_builder_t &index_maker_t::finish() noexcept {
    auto &cluster = builder.cluster;
    auto peer = builder.cluster.get_devices().by_sha256(peer_sha256);
    auto diff = diff::peer::update_folder_t::create(cluster, *builder.sequencer, *peer, index);
    assert(diff.has_value());
    builder.assign(diff.value().get());
    return builder;
}

diff_builder_t::diff_builder_t(model::cluster_t &cluster_, r::address_ptr_t receiver_) noexcept
    : cluster{cluster_}, receiver{receiver_} {
    sequencer = model::make_sequencer(0);
}

diff_builder_t &diff_builder_t::apply(rotor::supervisor_t &sup) noexcept {
    auto has_diffs = [&]() -> bool { return (bool)cluster_diff; };
    assert(has_diffs());

    if (receiver) {
        sup.send<model::payload::model_update_t>(receiver, std::move(cluster_diff), nullptr);
        sup.do_process();
    }

    auto &addr = sup.get_address();
    bool do_try = true;
    while (do_try) {
        do_try = false;
        if (cluster_diff) {
            sup.send<model::payload::model_update_t>(addr, std::move(cluster_diff), nullptr);
            do_try = true;
        }
        sup.do_process();
    }

    return *this;
}

auto diff_builder_t::apply() noexcept -> outcome::result<void> {
    auto r = outcome::result<void>(outcome::success());
    bool do_try = true;
    while (do_try) {
        do_try = false;
        if (r && cluster_diff) {
            r = cluster_diff->apply(cluster);
            cluster_diff.reset();
            do_try = true;
        }
    }
    return r;
}

diff_builder_t &diff_builder_t::upsert_folder(std::string_view id, std::string_view path, std::string_view label,
                                              std::uint64_t index_id) noexcept {
    db::Folder db_folder;
    db_folder.set_id(std::string(id));
    db_folder.set_label(std::string(label));
    db_folder.set_path(std::string(path));
    auto opt = diff::modify::upsert_folder_t::create(cluster, *sequencer, db_folder, index_id);
    return assign(opt.value().get());
}

diff_builder_t &diff_builder_t::upsert_folder(const db::Folder &data, std::uint64_t index_id) noexcept {
    auto opt = diff::modify::upsert_folder_t::create(cluster, *sequencer, data, index_id);
    return assign(opt.value().get());
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
    auto device = cluster.get_devices().by_sha256(sha256);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto opt = diff::modify::share_folder_t::create(cluster, *sequencer, *device, *folder);
    if (!opt) {
        spdlog::error("cannot share: {}", opt.assume_error().message());
        return *this;
    }
    return assign(opt.assume_value().get());
}

diff_builder_t &diff_builder_t::unshare_folder(model::folder_info_t &fi) noexcept {
    return assign(new diff::modify::unshare_folder_t(cluster, fi));
}

diff_builder_t &diff_builder_t::remote_copy(const model::file_info_t &source) noexcept {
    auto action = model::advance_action_t::remote_copy;
    auto diff = diff::advance::remote_copy_t::create(action, source, *sequencer);
    return assign(diff.get());
}

diff_builder_t &diff_builder_t::finish_file(const model::file_info_t &file) noexcept {
    return assign(new diff::modify::finish_file_t(file));
}

diff_builder_t &diff_builder_t::local_update(std::string_view folder_id, const proto::FileInfo &file_) noexcept {
    return assign(new diff::advance::local_update_t(cluster, *sequencer, file_, folder_id));
}

diff_builder_t &diff_builder_t::remove_peer(const model::device_t &peer) noexcept {
    return assign(new diff::modify::remove_peer_t(cluster, peer));
    return *this;
}

diff_builder_t &diff_builder_t::remove_folder(const model::folder_t &folder) noexcept {
    return assign(new diff::modify::remove_folder_t(cluster, *sequencer, folder));
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

diff_builder_t &diff_builder_t::append_block(const model::file_info_t &target, size_t block_index,
                                             std::string data) noexcept {
    return assign(new diff::modify::append_block_t(target, block_index, std::move(data)));
}

diff_builder_t &diff_builder_t::clone_block(const model::file_block_t &file_block) noexcept {
    return assign(new diff::modify::clone_block_t(file_block));
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
    return assign(new diff::modify::add_pending_device_t(device, db_device));
}

diff_builder_t &diff_builder_t::remove_ignored_device(const model::ignored_device_t &device) noexcept {
    return assign(new diff::modify::remove_ignored_device_t(device));
}

diff_builder_t &diff_builder_t::remove_unknown_device(const model::pending_device_t &device) noexcept {
    return assign(new diff::modify::remove_pending_device_t(device));
}

diff_builder_t &diff_builder_t::scan_start(std::string_view id, const r::pt::ptime &at) noexcept {
    auto final_at = at.is_not_a_date_time() ? r::pt::microsec_clock::local_time() : at;
    return assign(new model::diff::local::scan_start_t(std::string(id), final_at));
}

diff_builder_t &diff_builder_t::scan_finish(std::string_view id, const r::pt::ptime &at) noexcept {
    auto final_at = at.is_not_a_date_time() ? r::pt::microsec_clock::local_time() : at;
    return assign(new model::diff::local::scan_finish_t(std::string(id), final_at));
}

diff_builder_t &diff_builder_t::scan_request(std::string_view id) noexcept {
    return assign(new model::diff::local::scan_request_t(std::string(id)));
}

diff_builder_t &diff_builder_t::synchronization_start(std::string_view id) noexcept {
    return assign(new model::diff::local::synchronization_start_t(std::string(id)));
}

diff_builder_t &diff_builder_t::synchronization_finish(std::string_view id) noexcept {
    return assign(new model::diff::local::synchronization_finish_t(std::string(id)));
}

diff_builder_t &diff_builder_t::mark_reacheable(model::file_info_ptr_t peer_file, bool value) noexcept {
    return assign(new model::diff::modify::mark_reachable_t(*peer_file, value));
}

template <typename Holder, typename Diff> static void generic_assign(Holder *holder, Diff *diff) noexcept {
    if (!(*holder)) {
        holder->reset(diff);
    } else {
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

auto diff_builder_t::get_sequencer() noexcept -> model::sequencer_t & { return *sequencer; }
