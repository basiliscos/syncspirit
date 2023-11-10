// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "diff-builder.h"
#include "model/messages.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/create_folder.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/flush_file.h"
#include "model/diff/modify/local_update.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"

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
    builder.diffs.emplace_back(std::move(diff.value()));
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
    builder.diffs.emplace_back(std::move(diff.value()));
    return builder;
}

diff_builder_t::diff_builder_t(model::cluster_t &cluster_) noexcept : cluster{cluster_} {}

diff_builder_t &diff_builder_t::apply(rotor::supervisor_t &sup) noexcept {
    assert(!(diffs.empty() && bdiffs.empty()));
    auto diff = diff::cluster_diff_ptr_t(new diff::aggregate_t(std::move(diffs)));
    auto &addr = sup.get_address();
    sup.send<model::payload::model_update_t>(addr, std::move(diff), nullptr);
    diffs.clear();

    for (auto &diff : bdiffs) {
        sup.send<model::payload::block_update_t>(addr, std::move(diff), nullptr);
    }
    bdiffs.clear();

    sup.do_process();
    return *this;
}

auto diff_builder_t::apply() noexcept -> outcome::result<void> {
    auto r = outcome::result<void>(outcome::success());
    for (auto &d : diffs) {
        r = d->apply(cluster);
        if (!r) {
            return r;
        }
    }
    diffs.clear();

    for (auto &d : bdiffs) {
        r = d->apply(cluster);
        if (!r) {
            return r;
        }
    }
    bdiffs.clear();
    return r;
}

diff_builder_t &diff_builder_t::create_folder(std::string_view id, std::string_view path,
                                              std::string_view label) noexcept {
    db::Folder db_folder;
    db_folder.set_id(std::string(id));
    db_folder.set_label(std::string(label));
    db_folder.set_path(std::string(path));
    diffs.emplace_back(new diff::modify::create_folder_t(db_folder));
    return *this;
}

diff_builder_t &diff_builder_t::update_peer(std::string_view sha256, std::string_view name, std::string_view cert_name,
                                            bool auto_accept) noexcept {
    db::Device db_device;
    db_device.set_name(std::string(name));
    db_device.set_cert_name(std::string(cert_name));
    db_device.set_auto_accept(auto_accept);

    auto diff = diff::cluster_diff_ptr_t(new diff::modify::update_peer_t(db_device, sha256));
    diffs.emplace_back(std::move(diff));
    return *this;
}

cluster_configurer_t diff_builder_t::configure_cluster(std::string_view sha256) noexcept {
    return cluster_configurer_t(*this, sha256);
}

index_maker_t diff_builder_t::make_index(std::string_view sha256, std::string_view folder_id) noexcept {
    return index_maker_t(*this, sha256, folder_id);
}

diff_builder_t &diff_builder_t::share_folder(std::string_view sha256, std::string_view folder_id) noexcept {
    diffs.emplace_back(new diff::modify::share_folder_t(sha256, folder_id));
    return *this;
}

diff_builder_t &diff_builder_t::unshare_folder(std::string_view sha256, std::string_view folder_id) noexcept {
    diffs.emplace_back(new diff::modify::unshare_folder_t(cluster, sha256, folder_id));
    return *this;
}

diff_builder_t &diff_builder_t::clone_file(const model::file_info_t &source) noexcept {
    diffs.emplace_back(new diff::modify::clone_file_t(source));
    return *this;
}

diff_builder_t &diff_builder_t::finish_file(const model::file_info_t &source) noexcept {
    diffs.emplace_back(new diff::modify::finish_file_t(source));
    return *this;
}

diff_builder_t &diff_builder_t::flush_file(const model::file_info_t &source) noexcept {
    diffs.emplace_back(new diff::modify::flush_file_t(source));
    return *this;
}

diff_builder_t &diff_builder_t::local_update(std::string_view folder_id, const proto::FileInfo &file_) noexcept {
    diffs.emplace_back(new diff::modify::local_update_t(cluster, folder_id, file_));
    return *this;
}

diff_builder_t &diff_builder_t::append_block(const model::file_info_t &target, size_t block_index,
                                             std::string data) noexcept {
    bdiffs.emplace_back(new diff::modify::append_block_t(target, block_index, std::move(data)));
    return *this;
}

diff_builder_t &diff_builder_t::clone_block(const model::file_block_t &file_block) noexcept {
    bdiffs.emplace_back(new diff::modify::clone_block_t(file_block));
    return *this;
}
