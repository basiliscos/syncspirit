// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "controller_actor.h"
#include "names.h"
#include "constants.h"
#include "model/diff/advance/advance.h"
#include "model/diff/local/synchronization_finish.h"
#include "model/diff/local/synchronization_start.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/add_remote_folder_infos.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/modify/block_rej.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/mark_reachable.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"
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

struct context_t {
    bool from_self;
    bool cluster_config_sent;
};

} // namespace

using C = controller_actor_t;

C::folder_synchronization_t::folder_synchronization_t(controller_actor_t &controller_,
                                                      model::folder_t &folder_) noexcept
    : controller{controller_}, folder{&folder_}, synchronizing{false} {}

C::folder_synchronization_t::~folder_synchronization_t() {
    if (blocks.size()) {
        controller.push(new model::diff::local::synchronization_finish_t(folder->get_id()));
        for (auto &b : blocks) {
            b->unlock();
        }
    }
}

void C::folder_synchronization_t::start_fetching(model::block_info_t *block) noexcept {
    block->lock();
    if (blocks.empty() && !synchronizing) {
        start_sync();
    }
    blocks.emplace(block);
}

void C::folder_synchronization_t::finish_fetching(model::block_info_t *block) noexcept {
    block->unlock();
    blocks.erase(block);
    if (blocks.size() == 0 && synchronizing) {
        finish_sync();
    }
}

void C::folder_synchronization_t::start_sync() noexcept {
    controller.push(new model::diff::local::synchronization_start_t(folder->get_id()));
    synchronizing = true;
}

void C::folder_synchronization_t::finish_sync() noexcept {
    controller.push(new model::diff::local::synchronization_finish_t(folder->get_id()));
    synchronizing = false;
}

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, sequencer{std::move(config.sequencer)}, cluster{config.cluster}, peer{config.peer},
      peer_addr{config.peer_addr}, request_timeout{config.request_timeout}, rx_blocks_requested{0},
      tx_blocks_requested{0}, outgoing_buffer{0}, outgoing_buffer_max{config.outgoing_buffer_max},
      request_pool{config.request_pool}, blocks_max_requested{config.blocks_max_requested},
      advances_per_iteration{config.advances_per_iteration} {
    {
        assert(cluster);
        assert(sequencer);
        current_diff = nullptr;
        planned_pulls = 0;
        file_iterator = peer->create_iterator(*cluster);
    }
}

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
            }
        });
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(peer_addr, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
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
    peer->release_iterator(file_iterator);
    file_iterator.reset();
    synchronizing_folders.clear();
    send_diff();
    r::actor_base_t::shutdown_finish();
}

void controller_actor_t::send_cluster_config() noexcept {
    LOG_TRACE(log, "sending cluster config");
    auto cluster_config = cluster->generate(*peer);
    fmt::memory_buffer data;
    proto::serialize(data, cluster_config);
    outgoing_buffer += static_cast<uint32_t>(data.size());
    send<payload::transfer_data_t>(peer_addr, std::move(data));
    send_new_indices();
}

void controller_actor_t::send_new_indices() noexcept {
    if (updates_streamer) {
        auto &remote_folders = peer->get_remote_folder_infos();
        for (auto it : cluster->get_folders()) {
            auto &folder = *it.item;
            auto peer_folder = folder.is_shared_with(*peer);
            if (peer_folder) {
                auto local_folder = folder.get_folder_infos().by_device(*cluster->get_device());
                auto remote_folder = remote_folders.by_folder(folder);
                if (remote_folder && remote_folder->get_index() != local_folder->get_index()) {
                    LOG_DEBUG(log, "sending new index for folder '{}' ({})", folder.get_label(), folder.get_id());
                    proto::Index index;
                    index.set_folder(std::string(folder.get_id()));
                    fmt::memory_buffer data;
                    proto::serialize(data, index);
                    outgoing_buffer += static_cast<uint32_t>(data.size());
                    send<payload::transfer_data_t>(peer_addr, std::move(data));
                }
            }
        }
    }
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

void controller_actor_t::push_pending() noexcept {
    using pair_t = std::pair<model::folder_info_t *, proto::IndexUpdate>;
    using indices_t = std::vector<pair_t>;

    if (!updates_streamer) {
        return;
    }

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
    while (expected_sz < outgoing_buffer_max - outgoing_buffer) {
        if (auto file_info = updates_streamer->next(); file_info) {
            expected_sz += file_info->expected_meta_size();
            auto &index = get_index(*file_info);
            *index.add_files() = file_info->as_proto(true);
            LOG_TRACE(log, "pushing index update for: {}, seq = {}", file_info->get_full_name(),
                      file_info->get_sequence());
        } else {
            break;
        }
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

void controller_actor_t::push(model::diff::cluster_diff_ptr_t new_diff) noexcept {
    if (current_diff) {
        current_diff = current_diff->assign_sibling(new_diff.get());
    } else {
        diff = std::move(new_diff);
        current_diff = diff.get();
        while (current_diff->sibling) {
            current_diff = current_diff->sibling.get();
        }
    }
}

void controller_actor_t::pull_ready() noexcept { ++planned_pulls; }

void controller_actor_t::send_diff() noexcept {
    if (planned_pulls && !diff) {
        push(new pull_signal_t(this));
        planned_pulls = 0;
    }
    if (diff) {
        send<model::payload::model_update_t>(fs_addr, std::move(diff), this);
        current_diff = nullptr;
    }
}

void controller_actor_t::on_custom(const pull_signal_t &) noexcept { pull_next(); }

void controller_actor_t::pull_next() noexcept {
    LOG_TRACE(log, "pull_next (pull_signal_t), blocks requested = {}", rx_blocks_requested);
    auto advances = std::uint_fast32_t{0};
    auto can_pull_more = [&]() -> bool {
        bool ignore = (rx_blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                      || (state != r::state_t::OPERATIONAL) // request pool sz = 32505856e are shutting down
                      || !cluster->get_write_requests() || advances > advances_per_iteration;
        return !ignore;
    };

    using file_set_t = std::set<model::file_info_t *>;
    auto seen_files = file_set_t();
OUTER:
    while (can_pull_more()) {
        if (block_iterator) {
            seen_files.emplace(block_iterator->get_source().get());
            if (*block_iterator) {
                auto file_block = block_iterator->next();
                if (!file_block.block()->is_locked()) {
                    preprocess_block(file_block);
                }
                continue;
            } else {
                // file_iterator->commit_sync(block_iterator->get_source());
                block_iterator.reset();
            }
            continue;
        }
        if (!block_iterator && !synchronizing_files.empty()) {
            for (auto &[file, _] : synchronizing_files) {
                if (seen_files.contains(file)) {
                    continue;
                }
                auto bi = model::block_iterator_ptr_t();
                bi = new model::blocks_iterator_t(*file);
                if (bi) {
                    block_iterator = bi;
                    goto OUTER;
                }
            }
        }
        if (auto [file, action] = file_iterator->next(); action != model::advance_action_t::ignore) {
            auto _ = model::resolve(*file);
            auto in_sync = synchronizing_files.count(file);
            if (in_sync) {
                continue;
            }
            if (file->is_locally_available()) {
                auto diff = model::diff::advance::advance_t::create(*file, *sequencer);
                if (diff) {
                    ++advances;
                    push(std::move(diff));
                    LOG_TRACE(log, "going to advance on file '{}' from folder '{}'", file->get_name(),
                              file->get_folder_info()->get_folder()->get_label());
                }
            } else if (file->get_size()) {
                auto bi = model::block_iterator_ptr_t();
                bi = new model::blocks_iterator_t(*file);
                if (*bi) {
                    block_iterator = bi;
                    synchronizing_files[file] = file->guard();
                }
            }

            continue;
        }
        break;
    }
    if (diff) {
        pull_ready();
        send_diff();
    }
}

void controller_actor_t::preprocess_block(model::file_block_t &file_block) noexcept {
    using namespace model::diff;
    auto file = file_block.file();
    assert(file);
    auto block = file_block.block();
    acquire_block(file_block);

    if (file_block.is_locally_available()) {
        LOG_TRACE(log, "cloning locally available block, file = {}, block index = {} / {}", file->get_full_name(),
                  file_block.block_index(), file->get_blocks().size() - 1);
        auto diff = cluster_diff_ptr_t(new modify::clone_block_t(file_block));
        push_block_write(std::move(diff));
    } else {
        auto sz = block->get_size();
        LOG_TRACE(log, "request_block on file '{}'; block index = {} / {}, sz = {}, request pool sz = {}",
                  file->get_full_name(), file_block.block_index(), file->get_blocks().size() - 1, sz, request_pool);
        request<payload::block_request_t>(peer_addr, file, file_block).send(request_timeout);
        ++rx_blocks_requested;
        request_pool -= (int64_t)sz;
    }
}

void controller_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    if (state != r::state_t::OPERATIONAL) {
        return;
    }
    std::visit([this](auto &msg) { on_message(msg); }, message.payload);
}

void controller_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    auto custom = const_cast<void *>(message.payload.custom);
    auto pulls = std::uint32_t{0};
    if (custom == this) {
        pulls = planned_pulls;
        planned_pulls = 0;
    }
    auto ctx = context_t{custom == this, false};
    LOG_TRACE(log, "on_model_update, planned pulls = {}", pulls);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, &ctx);
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    pull_next();
    push_pending();
    send_diff();
}

auto controller_actor_t::operator()(const model::diff::peer::cluster_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<context_t *>(custom);
    if (ctx->from_self) {
        updates_streamer.reset(new model::updates_streamer_t(*cluster, *peer));
        send_new_indices();
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<context_t *>(custom);
    if (ctx->from_self) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto &files_map = folder->get_folder_infos().by_device(*peer)->get_file_infos();
        for (auto &f : diff.files) {
            auto file = files_map.by_name(f.name());
            auto it = synchronizing_files.find(file.get());
            if (it != synchronizing_files.end()) {
                synchronizing_files.erase(it);
            }
        }
        pull_ready();
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<context_t *>(custom);
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto folder_info = folder_infos.by_device_id(diff.peer_id);
    auto file = folder_info->get_file_infos().by_name(diff.proto_file.name());
    auto local_file = model::file_info_ptr_t();
    assert(file);
    if (diff.peer_id == peer->device_id().get_sha256()) {
        local_file = file->local_file();
        if (file->get_blocks().size()) {
            auto it = synchronizing_files.find(file.get());
            if (it != synchronizing_files.end()) {
                synchronizing_files.erase(it);
            }
        }
    }
    if (diff.peer_id == cluster->get_device()->device_id().get_sha256()) {
        local_file = file;
    }
    if (local_file) {
        if (updates_streamer) {
            updates_streamer->on_update(*local_file);
        }
        pull_ready();
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::add_remote_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.device_id == peer->device_id().get_sha256()) {
        if (updates_streamer) {
            updates_streamer->on_remote_refresh();
        }
        push_pending();
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto process = diff.device_id == peer->device_id().get_sha256();
    if (process) {
        auto ctx = reinterpret_cast<context_t *>(custom);
        if (!ctx->cluster_config_sent) {
            ctx->cluster_config_sent = true;
            send_cluster_config();
        }
        pull_ready();
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::remove_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    for (auto &key : diff.keys) {
        auto decomposed = model::folder_info_t::decompose_key(key);
        auto device_key = decomposed.device_key();
        if (device_key == peer->get_key() || device_key == cluster->get_device()->get_key()) {
            auto ctx = reinterpret_cast<context_t *>(custom);
            if (!ctx->cluster_config_sent) {
                ctx->cluster_config_sent = true;
                send_cluster_config();
            }
            break;
        }
    }

    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::mark_reachable_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<context_t *>(custom);
    auto &folder_id = diff.folder_id;
    auto &file_name = diff.file_name;
    auto folder = cluster->get_folders().by_id(folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto device = cluster->get_devices().by_sha256(diff.device_id);
    auto folder_info = folder_infos.by_device(*device);
    auto file = folder_info->get_file_infos().by_name(file_name);
    if (ctx->from_self) {
        auto it = synchronizing_files.find(file.get());
        if (it != synchronizing_files.end()) {
            synchronizing_files.erase(it);
        }
        pull_ready();
    }
    if (device == cluster->get_device() && updates_streamer) {
        updates_streamer->on_update(*file);
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::block_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {

    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = folder_info->get_file_infos().by_name(diff.file_name);
    auto block = file->get_blocks().at(diff.block_index);
    release_block({block.get(), file.get(), diff.block_index});
    if (file->is_locally_available()) {
        LOG_TRACE(log, "on_block_update, finalizing {}", file->get_name());
        push(new model::diff::modify::finish_file_t(*file));
    }

    cluster->modify_write_requests(1);
    pull_ready();
    process_block_write();
    send_diff();

    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::block_rej_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_ERROR(log, "on block rej, not implemented");
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::remove_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.get_peer_sha256() == peer->device_id().get_sha256()) {
        LOG_DEBUG(log, "on remove_peer_t, initiating self destruction");
        auto ec = utils::make_error_code(utils::error_code_t::peer_has_been_removed);
        auto reason = make_error(ec);
        do_shutdown(reason);
    }
    return diff.visit_next(*this, custom);
}

void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept {
    LOG_DEBUG(log, "on_message (ClusterConfig)");
    auto diff_opt = model::diff::peer::cluster_update_t::create(*cluster, *sequencer, *peer, *message);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &diff = diff_opt.assume_value();
    push(diff.get());
    pull_ready();
    send_diff();
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    auto &msg = *message;
    LOG_DEBUG(log, "on_message (Index)");
    auto diff_opt = model::diff::peer::update_folder_t::create(*cluster, *sequencer, *peer, *message);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &diff = diff_opt.assume_value();
    auto folder = cluster->get_folders().by_id(msg.folder());
    LOG_DEBUG(log, "on_message (Index), folder = {}, files = {}", folder->get_label(), msg.files_size());
    push(diff.get());
    pull_ready();
    send_diff();
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    LOG_TRACE(log, "on_message (IndexUpdate)");
    auto &msg = *message;
    auto diff_opt = model::diff::peer::update_folder_t::create(*cluster, *sequencer, *peer, *message);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &diff = diff_opt.assume_value();
    auto folder = cluster->get_folders().by_id(msg.folder());
    LOG_DEBUG(log, "on_message (IndexUpdate), folder = {}, files = {}", folder->get_label(), msg.files_size());
    push(diff.get());
    pull_ready();
    send_diff();
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
    bool try_next = true;

    if (ee) {
        release_block(file_block);
        auto &ec = ee->root()->ec;
        if (ec.category() == utils::request_error_code_category()) {
            auto file = file_block.file();
            if (!file->is_unreachable()) {
                LOG_WARN(log, "can't receive block from file '{}': {}; marking unreachable", file->get_full_name(),
                         ec.message());
                file->mark_unreachable(true);
                push(new model::diff::modify::mark_reachable_t(*file, false));
                auto it = synchronizing_files.find(file);
                synchronizing_files.erase(it);
            }
        } else {
            LOG_WARN(log, "can't receive block : {}", ee->message());
            do_shutdown(ee);
            try_next = false;
        }
    } else {
        auto &data = message.payload.res.data;
        auto hash = std::string(file_block.block()->get_hash());
        request_pool += block.get_size();

        request<hasher::payload::validation_request_t>(hasher_proxy, data, hash, &message).send(init_timeout);
        resources->acquire(resource::hash);
    }

    if (try_next) {
        pull_ready();
    }
    send_diff();
}

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    using namespace model::diff;
    resources->release(resource::hash);
    auto &ee = res.payload.ee;
    auto block_res = (message::block_response_t *)res.payload.req->payload.request_payload->custom.get();
    auto &payload = block_res->payload.req->payload.request_payload;
    auto index = payload.block.block_index();
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
                push(new model::diff::modify::mark_reachable_t(*file, false));
            }
            release_block({file->get_blocks()[index].get(), file.get(), index});
            pull_ready();
            send_diff();
        } else {
            auto &data = block_res->payload.res.data;

            LOG_TRACE(log, "{}, got block {}, write requests left = {}", file->get_name(), index,
                      cluster->get_write_requests());
            auto diff = cluster_diff_ptr_t(new modify::append_block_t(*file, index, std::move(data)));
            push_block_write(std::move(diff));
        }
    }
}

void controller_actor_t::push_block_write(model::diff::cluster_diff_ptr_t diff) noexcept {
    block_write_queue.emplace_back(std::move(diff));
    process_block_write();
}

void controller_actor_t::process_block_write() noexcept {
    auto requests_left = cluster->get_write_requests();
    auto sent = 0;
    while (requests_left > 0 && !block_write_queue.empty()) {
        auto &diff = block_write_queue.front();
        push(diff.get());
        --requests_left;
        ++sent;
        block_write_queue.pop_front();
    }
    if (sent) {
        send_diff();
        LOG_TRACE(log, "{} block writes sent, requests left = {}", sent, requests_left);
        cluster->modify_write_requests(-sent);
    }
}

auto controller_actor_t::get_sync_info(model::folder_t *folder) noexcept -> folder_synchronization_ptr_t {
    auto it = synchronizing_folders.find(folder);
    if (it == synchronizing_folders.end()) {
        auto info = folder_synchronization_ptr_t(new folder_synchronization_t(*this, *folder));
        synchronizing_folders.emplace(folder, info);
        return info;
    }
    return it->second;
}

void controller_actor_t::acquire_block(const model::file_block_t &file_block) noexcept {
    auto block = file_block.block();
    auto folder = file_block.file()->get_folder_info()->get_folder();
    auto it = synchronizing_folders.find(folder);
    get_sync_info(folder)->start_fetching(block);
}

void controller_actor_t::release_block(const model::file_block_t &file_block) noexcept {
    auto block = file_block.block();
    auto folder = file_block.file()->get_folder_info()->get_folder();
    get_sync_info(folder)->finish_fetching(block);
}

controller_actor_t::pull_signal_t::pull_signal_t(void *controller_) noexcept : controller{controller_} {}

auto controller_actor_t::pull_signal_t::visit(model::diff::cluster_visitor_t &visitor, void *custom) const noexcept
    -> outcome::result<void> {
    auto r = visitor(*this, custom);
    auto ctx = reinterpret_cast<context_t *>(custom);
    auto self = static_cast<controller_actor_t *>(&visitor);
    if (r && self == controller && ctx->from_self) {
        self->on_custom(*this);
    }
    return r;
}
