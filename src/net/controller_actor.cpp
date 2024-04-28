// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "controller_actor.h"
#include "names.h"
#include "constants.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/modify/block_rej.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/local_update.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/modify/mark_reachable.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/finish_file_ack.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/modify/unshare_folder.h"
#include "proto/bep_support.h"
#include "utils/error_code.h"
#include "utils/format.hpp"

#include <utility>

using namespace syncspirit;
using namespace syncspirit::net;
namespace bfs = boost::filesystem;

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
r::plugin::resource_id_t hash = 1;
} // namespace resource
} // namespace

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster}, peer{config.peer}, peer_addr{config.peer_addr},
      request_timeout{config.request_timeout}, rx_blocks_requested{0}, tx_blocks_requested{0}, outgoing_buffer{0},
      outgoing_buffer_max{config.outgoing_buffer_max}, request_pool{config.request_pool},
      blocks_max_requested{config.blocks_max_requested} {}

void controller_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "net.controller/";
        id += peer->device_id().get_short();
        p.set_identity(id, false);
        log = utils::get_logger(identity);
        open_reading = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::fs_actor, fs_addr, false).link();
        p.discover_name(names::hasher_proxy, hasher_proxy, false).link();
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&controller_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&controller_actor_t::on_block_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(peer_addr, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
        p.subscribe_actor(&controller_actor_t::on_pull_ready);
        p.subscribe_actor(&controller_actor_t::on_termination);
        p.subscribe_actor(&controller_actor_t::on_block);
        p.subscribe_actor(&controller_actor_t::on_transfer_pop);
        p.subscribe_actor(&controller_actor_t::on_transfer_push);
        p.subscribe_actor(&controller_actor_t::on_validation);
        p.subscribe_actor(&controller_actor_t::on_block_response);
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_start");
    send<payload::start_reading_t>(peer_addr, get_address(), true);

    send_cluster_config();

    resources->acquire(resource::peer);
    LOG_INFO(log, "is online");
}

void controller_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    if (peer_addr) {
        send<payload::termination_t>(peer_addr, shutdown_reason);
    }
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish, blocks_requested = {}", rx_blocks_requested);
    if (!locked_files.empty()) {
        using diffs_t = model::diff::aggregate_t::diffs_t;
        auto diffs = diffs_t{};
        for (auto &file : locked_files) {
            if (!file->is_unlocking()) {
                LOG_TRACE(log, "going to unlock {} ({}); is_unlocking {}", file->get_full_name(), (void *)file.get(),
                          file->is_unlocking());
                diffs.push_back(new model::diff::modify::lock_file_t(*file, false));
            }
            file->set_unlocking(false);
        }
        for (auto &file : locally_locked_files) {
            file->locally_unlock();
        }

        LOG_DEBUG(log, "unlocking {} model files and {} local files", diffs.size(), locally_locked_files.size());
        auto diff = model::diff::cluster_diff_ptr_t{};
        diff = new model::diff::aggregate_t(std::move(diffs));
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    }
    r::actor_base_t::shutdown_finish();
}

void controller_actor_t::send_cluster_config() noexcept {
    LOG_TRACE(log, "sending cluster config");
    auto cluster_config = cluster->generate(*peer);
    fmt::memory_buffer data;
    proto::serialize(data, cluster_config);
    outgoing_buffer += static_cast<uint32_t>(data.size());
    send<payload::transfer_data_t>(peer_addr, std::move(data));
}

void controller_actor_t::on_transfer_push(message::transfer_push_t &message) noexcept {
    auto sz = message.payload.bytes;
    outgoing_buffer += sz;
    LOG_TRACE(log, "on_transfer_push, sz = {} (+{})", outgoing_buffer, sz);
}

void controller_actor_t::on_transfer_pop(message::transfer_pop_t &message) noexcept {
    auto sz = message.payload.bytes;
    assert(outgoing_buffer >= sz);
    outgoing_buffer -= sz;
    LOG_TRACE(log, "on_transfer_pop, sz = {} (-{})", outgoing_buffer, sz);
    push_pending();
}

void controller_actor_t::on_termination(message::termination_signal_t &message) noexcept {
    if (resources->has(resource::peer)) {
        resources->release(resource::peer);
        peer_addr.reset();
    }
    auto &ee = message.payload.ee;
    LOG_TRACE(log, "on_termination reason: {}", ee->message());
    do_shutdown(ee);
}

void controller_actor_t::pull_ready() noexcept { send<payload::pull_signal_t>(get_address()); }

void controller_actor_t::push_pending() noexcept {
    using pair_t = std::pair<model::folder_info_t *, proto::IndexUpdate>;
    using indices_t = std::vector<pair_t>;

    auto indices = indices_t{};
    auto get_index = [&](model::file_info_t &file) -> proto::IndexUpdate & {
        auto folder_info = file.get_folder_info();
        for (auto &p : indices) {
            if (p.first == folder_info) {
                return p.second;
            }
        }
        indices.emplace_back(folder_info, proto::IndexUpdate());
        auto &index = indices.back().second;
        index.set_folder(std::string(folder_info->get_folder()->get_id()));
        return index;
    };

    auto expected_sz = 0;
    while (updates_streamer && (expected_sz < outgoing_buffer_max - outgoing_buffer)) {
        auto file_info = updates_streamer.next();
        expected_sz += file_info->expected_meta_size();
        auto &index = get_index(*file_info);
        *index.add_files() = file_info->as_proto(true);
        LOG_TRACE(log, "pushing index update for: {}, seq = {}", file_info->get_full_name(), file_info->get_sequence());
    }

    fmt::memory_buffer data;
    for (auto &p : indices) {
        auto &index = p.second;
        if (index.files_size() > 0) {
            proto::serialize(data, index);
            outgoing_buffer += static_cast<uint32_t>(data.size());
            send<payload::transfer_data_t>(peer_addr, std::move(data));
        }
    }
}

model::file_info_ptr_t controller_actor_t::next_file(bool reset) noexcept {
    if (reset) {
        file_iterator = new model::file_iterator_t(*cluster, peer);
    }
    if (file_iterator && *file_iterator) {
        return file_iterator->next();
    }
    return {};
}

model::file_block_t controller_actor_t::next_block(bool reset) noexcept {
    if (file->is_file() && !file->is_deleted()) {
        if (reset) {
            block_iterator = new model::blocks_iterator_t(*file);
        }
        if (block_iterator && *block_iterator) {
            return block_iterator->next(!reset);
        }
    }
    return {};
}

void controller_actor_t::on_pull_ready(message::pull_signal_t &) noexcept {
    LOG_TRACE(log, "on_pull_ready, blocks requested = {}", rx_blocks_requested);
    bool ignore = (rx_blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                  || (state != r::state_t::OPERATIONAL) // request pool sz = 32505856e are shutting down
        ;
    if (ignore) {
        return;
    }

    if (file && file->get_size() && file->local_file() && cluster->get_write_requests()) {
        auto reset_block = !(substate & substate_t::iterating_blocks);
        auto block = next_block(reset_block);
        if (block) {
            substate |= substate_t::iterating_blocks;
            if (reset_block) {
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::lock_file_t(*file, true);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            } else {
                preprocess_block(block);
            }
        } else {
            substate = substate & ~substate_t::iterating_blocks;
        }
    }
    if (!(substate & substate_t::iterating_blocks)) {
        bool reset_file = !(substate & substate_t::iterating_files);
        file = next_file(reset_file);
        if (file) {
            substate |= substate_t::iterating_files;
            if (!file->local_file()) {
                LOG_DEBUG(log, "next_file = {}", file->get_name());
                file->locally_lock();
                locally_locked_files.emplace(file);
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::clone_file_t(*file);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            } else {
                pull_ready();
            }
        } else {
            substate = substate & ~substate_t::iterating_files;
        }
    }
}

void controller_actor_t::preprocess_block(model::file_block_t &file_block) noexcept {
    using namespace model::diff;
    assert(file->local_file());

    if (file_block.is_locally_available()) {
        LOG_TRACE(log, "cloning locally available block, file = {}, block index = {} / {}", file->get_full_name(),
                  file_block.block_index(), file->get_blocks().size() - 1);
        auto diff = block_diff_ptr_t(new modify::clone_block_t(file_block, make_callback()));
        push_block_write(std::move(diff));
    } else {
        auto block = file_block.block();
        auto sz = block->get_size();
        LOG_TRACE(log, "request_block on file '{}'; block index = {} / {}, sz = {}, request pool sz = {}",
                  file->get_full_name(), file_block.block_index(), file->get_blocks().size() - 1, sz, request_pool);
        request<payload::block_request_t>(peer_addr, file, file_block).send(request_timeout);
        ++rx_blocks_requested;
        request_pool -= (int64_t)sz;
    }
    pull_ready();
}

void controller_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    if (state != r::state_t::OPERATIONAL) {
        return;
    }
    std::visit([this](auto &msg) { on_message(msg); }, message.payload);
}

void controller_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, const_cast<void *>(message.payload.custom));
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    pull_ready();
    push_pending();
}
auto controller_actor_t::operator()(const model::diff::peer::cluster_update_t &, void *custom) noexcept
    -> outcome::result<void> {
    if (custom != this) {
        return outcome::success();
    }

    for (auto it : cluster->get_folders()) {
        auto &folder = *it.item;
        auto folder_info = folder.is_shared_with(*peer);
        if (folder_info) {
            auto index_opt = folder_info->generate();
            if (index_opt) {
                LOG_DEBUG(log, "sending new index");
                auto index = *index_opt;
                fmt::memory_buffer data;
                proto::serialize(data, index);
                outgoing_buffer += static_cast<uint32_t>(data.size());
                send<payload::transfer_data_t>(peer_addr, std::move(data));
            }
        }
    }
    updates_streamer = model::updates_streamer_t(*cluster, *peer);

    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::clone_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (custom != this) {
        return outcome::success();
    }

    auto folder_id = diff.folder_id;
    auto file_name = diff.file.name();
    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*peer);
    auto file = folder_info->get_file_infos().by_name(file_name);
    file->locally_unlock();
    auto it = locally_locked_files.find(file);
    locally_locked_files.erase(it);
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::finish_file_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*peer);
    auto file = folder_info->get_file_infos().by_name(diff.file_name);
    assert(file);
    updates_streamer.on_update(*file);
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::lock_file_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (custom != this) {
        return outcome::success();
    }

    auto &folder_id = diff.folder_id;
    auto &file_name = diff.file_name;
    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*peer);
    auto file = folder_info->get_file_infos().by_name(file_name);
    if (diff.locked) {
        locked_files.emplace(std::move(file));
    } else {
        file->set_unlocking(false);
        auto it = locked_files.find(file);
        assert(it != locked_files.end());
        locked_files.erase(it);
    }
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::share_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.peer_id != peer->device_id().get_sha256()) {
        return outcome::success();
    }

    send_cluster_config();
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::unshare_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.peer_id != peer->device_id().get_sha256()) {
        return outcome::success();
    }

    send_cluster_config();
    if (updates_streamer) {
        updates_streamer = model::updates_streamer_t(*cluster, *peer);
    }

    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::local_update_t &diff, void *) noexcept
    -> outcome::result<void> {

    auto &folder_id = diff.folder_id;
    auto &file_name = diff.file.name();
    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*cluster->get_device());
    auto file = folder_info->get_file_infos().by_name(file_name);
    updates_streamer.on_update(*file);
    push_pending();
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::mark_reachable_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (custom == this) {
        auto &folder_id = diff.folder_id;
        auto &file_name = diff.file_name;
        auto folder = cluster->get_folders().by_id(folder_id);
        auto &folder_infos = folder->get_folder_infos();
        auto device = cluster->get_devices().by_sha256(diff.device_id);
        auto folder_info = folder_infos.by_device(*device);
        auto file = folder_info->get_file_infos().by_name(file_name);
        auto diff = model::diff::cluster_diff_ptr_t{};
        file->set_unlocking(true);
        diff = new model::diff::modify::lock_file_t(*file, false);
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    }
    push_pending();
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::block_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {

    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto source_file = folder_info->get_file_infos().by_name(diff.file_name);
    if (source_file->is_locally_available()) {
        using diffs_t = model::diff::aggregate_t::diffs_t;
        LOG_TRACE(log, "on_block_update, finalizing {}", source_file->get_name());
        auto my_file = source_file->local_file();
        auto diffs = diffs_t{};
        source_file->set_unlocking(true);
        diffs.push_back(new model::diff::modify::lock_file_t(*source_file, false));
        diffs.push_back(new model::diff::modify::finish_file_t(*my_file));
        auto diff = model::diff::cluster_diff_ptr_t{};
        diff = new model::diff::aggregate_t(std::move(diffs));
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);
    }

    cluster->modify_write_requests(1);
    process_block_write();
    pull_ready();

    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::block_rej_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_ERROR(log, "on block rej, not implemented");
    return outcome::success();
}

void controller_actor_t::on_block_update(model::message::block_update_t &message) noexcept {
    if (message.payload.custom == this) {
        LOG_TRACE(log, "on_block_update");
        auto &diff = *message.payload.diff;
        auto r = diff.visit(*this, const_cast<void *>(message.payload.custom));
        if (!r) {
            auto ee = make_error(r.assume_error());
            return do_shutdown(ee);
        }
    }
}

void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept {
    LOG_DEBUG(log, "on_message (ClusterConfig)");
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    auto &msg = *message;
    LOG_DEBUG(log, "on_message (Index)");
    auto diff_opt = cluster->process(msg, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto folder = cluster->get_folders().by_id(msg.folder());
    LOG_DEBUG(log, "on_message (Index), folder = {}, files = {}", folder->get_label(), msg.files_size());
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    LOG_TRACE(log, "on_message (IndexUpdate)");
    auto &msg = *message;
    auto diff_opt = cluster->process(msg, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto folder = cluster->get_folders().by_id(msg.folder());
    LOG_DEBUG(log, "on_message (IndexUpdate), folder = {}, files = {}", folder->get_label(), msg.files_size());
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Request &req) noexcept {
    proto::Response res;
    fmt::memory_buffer data;
    auto code = proto::ErrorCode::NO_BEP_ERROR;

    if (tx_blocks_requested > blocks_max_requested * constants::tx_blocks_max_factor) {
        LOG_WARN(log, "peer requesting too many blocks ({}), rejecting current request", tx_blocks_requested);
        code = proto::ErrorCode::GENERIC;
    } else {
        auto folder = cluster->get_folders().by_id(req->folder());
        if (!folder) {
            code = proto::ErrorCode::NO_SUCH_FILE;
        } else {
            auto &folder_infos = folder->get_folder_infos();
            auto peer_folder = folder_infos.by_device(*peer);
            if (!peer_folder) {
                code = proto::ErrorCode::NO_SUCH_FILE;
            } else {
                auto my_folder = folder_infos.by_device(*cluster->get_device());
                auto file = my_folder->get_file_infos().by_name(req->name());
                if (!file) {
                    code = proto::ErrorCode::NO_SUCH_FILE;
                } else {
                    if (!file->is_file()) {
                        LOG_WARN(log, "attempt to request non-regular file: {}", file->get_name());
                        code = proto::ErrorCode::GENERIC;
                    }
                }
            }
        }
    }

    if (code != proto::ErrorCode::NO_BEP_ERROR) {
        res.set_id(req->id());
        res.set_code(code);
        proto::serialize(data, res);
        outgoing_buffer += static_cast<uint32_t>(data.size());
        send<payload::transfer_data_t>(peer_addr, std::move(data));
    } else {
        ++tx_blocks_requested;
        send<fs::payload::block_request_t>(fs_addr, std::move(req), address);
    }
}

void controller_actor_t::on_message(proto::message::DownloadProgress &) noexcept { std::abort(); }

void controller_actor_t::on_block_response(fs::message::block_response_t &message) noexcept {
    --tx_blocks_requested;
    auto &p = message.payload;
    proto::Response res;
    res.set_id(p.remote_request->id());
    if (p.ec) {
        res.set_code(proto::ErrorCode::GENERIC);
    } else {
        res.set_data(std::move(p.data));
    }

    fmt::memory_buffer data;
    proto::serialize(data, res);
    outgoing_buffer += static_cast<uint32_t>(data.size());
    send<payload::transfer_data_t>(peer_addr, std::move(data));
}

void controller_actor_t::on_block(message::block_response_t &message) noexcept {
    --rx_blocks_requested;
    auto ee = message.payload.ee;
    auto &payload = message.payload.req->payload.request_payload;
    auto &file_block = payload.block;
    auto &block = *file_block.block();

    if (ee) {
        auto &ec = ee->root()->ec;
        if (ec.category() == utils::request_error_code_category()) {
            auto file = file_block.file();
            if (!file->is_unreachable()) {
                LOG_WARN(log, "can't receive block from file '{}': {}; marking unreachable", file->get_full_name(),
                         ec.message());
                file->mark_unreachable(true);
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::mark_reachable_t(*file, false);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            }
            pull_ready();
        } else {
            LOG_WARN(log, "can't receive block : {}", ee->message());
            do_shutdown(ee);
        }
        return;
    }

    auto &data = message.payload.res.data;
    auto hash = std::string(file_block.block()->get_hash());
    request_pool += block.get_size();

    request<hasher::payload::validation_request_t>(hasher_proxy, data, hash, &message).send(init_timeout);
    resources->acquire(resource::hash);
    return pull_ready();
}

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    using namespace model::diff;
    resources->release(resource::hash);
    auto &ee = res.payload.ee;
    auto block_res = (message::block_response_t *)res.payload.req->payload.request_payload->custom.get();
    auto &payload = block_res->payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &path = file->get_path();

    if (ee) {
        LOG_WARN(log, "on_validation failed : {}", ee->message());
        do_shutdown(ee);
    } else {
        if (!res.payload.res.valid) {
            if (!file->is_unreachable()) {
                auto ec = utils::make_error_code(utils::protocol_error_code_t::digest_mismatch);
                LOG_WARN(log, "digest mismatch for '{}'; marking reachable", file->get_full_name(), ec.message());
                file->mark_unreachable(true);
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::mark_reachable_t(*file, false);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            }
            pull_ready();
        } else {
            auto &data = block_res->payload.res.data;
            auto index = payload.block.block_index();

            LOG_TRACE(log, "{}, got block {}, write requests left = {}", file->get_name(), index,
                      cluster->get_write_requests());
            auto diff = block_diff_ptr_t(new modify::append_block_t(*file, index, std::move(data), make_callback()));
            push_block_write(std::move(diff));
        }
    }
}

#if 0
void controller_actor_t::on_write_ack(model::message::write_ack_t &message) noexcept {
    cluster->modify_write_requests(1);
    if (message.payload.custom == this) {
        process_block_write();
        pull_ready();
    }
}
#endif

void controller_actor_t::push_block_write(model::diff::block_diff_ptr_t diff) noexcept {
    block_write_queue.emplace_back(std::move(diff));
    process_block_write();
}

void controller_actor_t::process_block_write() noexcept {
    auto requests_left = cluster->get_write_requests();
    auto sent = 0;
    while (requests_left > 0 && !block_write_queue.empty()) {
        auto &diff = block_write_queue.front();
        send<model::payload::block_update_t>(coordinator, std::move(diff), this);
        --requests_left;
        ++sent;
        block_write_queue.pop_front();
    }
    if (sent) {
        LOG_TRACE(log, "{} block writes sent, requests left = {}", sent, requests_left);
        cluster->modify_write_requests(-sent);
    }
}

auto controller_actor_t::make_callback() noexcept -> dispose_callback_t {
    using pointer_t = r::intrusive_ptr_t<controller_actor_t>;
    auto address = coordinator;
    auto sup = r::supervisor_ptr_t{supervisor};
    return [sup = std::move(sup), address = std::move(address),
            this](model::diff::modify::block_transaction_t &source_diff) {
        bool ok = source_diff.errors.load() == 0;
        auto diff = model::diff::block_diff_ptr_t{};
        if (ok) {
            diff = new model::diff::modify::block_ack_t(source_diff);
        } else {
            diff = new model::diff::modify::block_rej_t(source_diff);
        }
        auto msg = r::make_message<model::payload::block_update_t>(coordinator, std::move(diff), this);
        sup->enqueue(std::move(msg));
    };
}
