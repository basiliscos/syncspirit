// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "controller_actor.h"
#include "names.h"
#include "constants.h"
#include "model/diff/advance/advance.h"
#include "model/diff/contact/peer_state.h"
#include "model/diff/local/synchronization_finish.h"
#include "model/diff/local/synchronization_start.h"
#include "model/diff/modify/block_ack.h"
#include "model/diff/modify/mark_reachable.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_files.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/peer/update_folder.h"
#include "model/misc/resolver.h"
#include "presentation/presence.h"
#include "proto/bep_support.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "utils/error_code.h"
#include "utils/format.hpp"
#include "utils/platform.h"

#include <utility>
#include <type_traits>
#include <memory_resource>

using namespace syncspirit;
using namespace syncspirit::net;
namespace bfs = std::filesystem;

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
r::plugin::resource_id_t hash = 1;
r::plugin::resource_id_t fs = 2;
} // namespace resource

struct remote_copy_context_t final : fs::payload::extendended_context_t {
    remote_copy_context_t(model::advance_action_t action_, model::file_info_t &peer_file_,
                          model::folder_info_t &peer_folder_)
        : peer_file(&peer_file_), peer_folder(&peer_folder_), action{action_} {}

    model::file_info_ptr_t peer_file;
    model::folder_info_ptr_t peer_folder;
    model::advance_action_t action;
};

struct block_ack_context_t final : fs::payload::extendended_context_t {
    block_ack_context_t(model::block_info_t *block_, model::file_info_t &target_file_,
                        model::folder_info_t &target_folder_, std::uint32_t block_index_)
        : block{block_}, target_file(&target_file_), target_folder(&target_folder_),
          folder(target_folder_.get_folder()), block_index{block_index_} {}

    model::block_info_ptr_t block;
    model::file_info_ptr_t target_file;
    model::folder_info_ptr_t target_folder;
    model::folder_ptr_t folder;
    std::uint32_t block_index;
};

struct finish_file_context_t final : fs::payload::extendended_context_t {
    finish_file_context_t(model::file_info_t &peer_file_, model::folder_info_t &peer_folder_,
                          model::advance_action_t action_)
        : peer_file(&peer_file_), peer_folder(&peer_folder_), action{action_} {}

    model::file_info_ptr_t peer_file;
    model::folder_info_ptr_t peer_folder;
    model::advance_action_t action;
};

struct block_request_context_t final : fs::payload::extendended_context_t {
    block_request_context_t(proto::Request request_) noexcept : request{std::move(request_)} {}

    proto::Request request;
};

struct peer_request_context_t final : fs::payload::extendended_context_t {
    peer_request_context_t(proto::Request request_, std::int64_t sequence_, std::int32_t block_index_) noexcept
        : request{std::move(request_)}, sequence{sequence_}, block_index{block_index_} {}

    proto::Request request;
    std::int64_t sequence;
    std::int32_t block_index;
};

} // namespace

using C = controller_actor_t;

C::stack_context_t::stack_context_t(controller_actor_t &actor_) noexcept : actor{actor_} {}

C::stack_context_t::~stack_context_t() {
    if (actor.state == r::state_t::OPERATIONAL) {
        auto requests_left = actor.cluster->get_write_requests();
        auto sent = 0;
        while (requests_left > 0 && !actor.block_write_queue.empty()) {
            auto &io_command = actor.block_write_queue.front();
            io_commands.emplace_back(std::move(io_command));
            --requests_left;
            ++sent;
            actor.block_write_queue.pop_front();
        }
        if (sent) {
            // LOG_TRACE(log, "{} block writes sent, requests left = {}", sent, requests_left);
            actor.cluster->modify_write_requests(-sent);
        }
        auto max_block_read = actor.blocks_max_requested * constants::tx_blocks_max_factor;
        while (!actor.block_read_queue.empty() && (actor.tx_blocks_requested <= max_block_read)) {
            ++actor.tx_blocks_requested;
            auto &cmd = actor.block_read_queue.front();
            io_commands.emplace_back(std::move(cmd));
            actor.block_read_queue.pop_front();
        }
        if (!io_commands.empty()) {
            auto &self = actor.get_address();
            auto &fs = actor.fs_addr;
            auto cache_key = actor.get_address().get();
            actor.route<fs::payload::io_commands_t>(fs, self, cache_key, std::move(io_commands));
            actor.resources->acquire(resource::fs);
        }
    }
    if (next) {
        auto &addr = actor.coordinator;
        actor.send<model::payload::model_update_t>(addr, std::move(diff), &actor);
    }
    if (!peer_data.empty()) {
        if (actor.peer_address) {
            *actor.outgoing_buffer += static_cast<uint32_t>(peer_data.size());
            actor.send<payload::transfer_data_t>(actor.peer_address, std::move(peer_data));
        } else {
            LOG_DEBUG(actor.log, "peer is no longer available, send has been ignored");
        }
    }
}

void C::stack_context_t::push(fs::payload::io_command_t command) noexcept {
    io_commands.emplace_back(std::move(command));
}

void C::stack_context_t::push(fs::payload::append_block_t command) noexcept {
    if (actor.state == r::state_t::OPERATIONAL) {
        auto requests_left = actor.cluster->get_write_requests();
        if (requests_left) {
            io_commands.emplace_back(std::move(command));
            actor.cluster->modify_write_requests(-1);
        } else {
            actor.block_write_queue.emplace_back(std::move(command));
        }
    }
}

void C::stack_context_t::push(model::diff::cluster_diff_ptr_t diff_) noexcept {
    if (next) {
        next = next->assign_sibling(diff_.get());
    } else {
        diff = std::move(diff_);
        next = diff.get();
    }
}

void C::stack_context_t::push(utils::bytes_t data) noexcept {
    peer_data.reserve(peer_data.size() + data.size());
    auto out = std::back_insert_iterator(peer_data);
    std::copy(data.begin(), data.end(), out);
}

C::update_context_t::update_context_t(controller_actor_t &actor, bool from_self_, bool cluster_config_sent_) noexcept
    : stack_context_t(actor), from_self{from_self_}, cluster_config_sent{cluster_config_sent_} {}

C::folder_synchronization_t::folder_synchronization_t(controller_actor_t &controller_,
                                                      model::folder_t &folder_) noexcept
    : controller{&controller_}, folder{&folder_}, synchronizing{false} {}

C::folder_synchronization_t::~folder_synchronization_t() {
    if (blocks.size() && folder && !folder->is_suspended()) {
        if (synchronizing) {
            auto diff = model::diff::cluster_diff_ptr_t();
            diff.reset(new model::diff::local::synchronization_finish_t(folder->get_id()));
            controller->send<model::payload::model_update_t>(controller->coordinator, std::move(diff), controller);
        }
        for (auto &it : blocks) {
            it.second->unlock();
        }
    }
}

void C::folder_synchronization_t::reset() noexcept { folder.reset(); }

void C::folder_synchronization_t::start_fetching(model::block_info_t *block, stack_context_t &context) noexcept {
    assert(!block->is_locked());
    assert(blocks.find(block->get_hash()) == blocks.end());
    block->lock();
    if (blocks.empty() && !synchronizing) {
        start_sync(context);
    }
    blocks[block->get_hash()] = model::block_info_ptr_t(block);
}

auto C::folder_synchronization_t::finish_fetching(utils::bytes_view_t hash, stack_context_t &context) noexcept
    -> model::block_info_ptr_t {
    auto it = blocks.find(hash);
    auto block = it->second;
    block->unlock();
    assert(!block->is_locked());
    blocks.erase(it);
    if (blocks.size() == 0 && synchronizing) {
        finish_sync(context);
    }
    return block;
}

void C::folder_synchronization_t::start_sync(stack_context_t &context) noexcept {
    context.push(new model::diff::local::synchronization_start_t(folder->get_id()));
    synchronizing = true;
}

void C::folder_synchronization_t::finish_sync(stack_context_t &context) noexcept {
    context.push(new model::diff::local::synchronization_finish_t(folder->get_id()));
    synchronizing = false;
}

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, sequencer{std::move(config.sequencer)}, cluster{config.cluster}, peer{config.peer},
      peer_state{peer->get_state().clone()}, peer_address{config.peer_addr}, rx_blocks_requested{0},
      tx_blocks_requested{0}, outgoing_buffer_max{config.outgoing_buffer_max}, request_pool{config.request_pool},
      hasher_threads{config.hasher_threads}, advances_per_iteration{config.advances_per_iteration},
      default_path(std::move(config.default_path)), announced{false} {
    {
        assert(cluster);
        assert(sequencer);
        assert(peer_state.is_online());
        assert(hasher_threads);
        blocks_max_requested = config.blocks_max_requested ? config.blocks_max_requested : hasher_threads * 2;
        outgoing_buffer.reset(new std::uint32_t(0));
        block_requests.resize(blocks_max_requested);
    }
}

void controller_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "net.controller/";
        id += peer->device_id().get_short();
        p.set_identity(id, false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<hasher::hasher_plugin_t>([&](auto &p) {
        hasher = &p;
        p.configure_hashers(hasher_threads);
        p.discover_name(names::fs_actor, fs_addr, false).link(false);
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&controller_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&controller_actor_t::on_fs_predown, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(peer_address, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
        p.subscribe_actor(&controller_actor_t::on_peer_down);
        p.subscribe_actor(&controller_actor_t::on_digest);
        p.subscribe_actor(&controller_actor_t::on_tx_signal);
        p.subscribe_actor(&controller_actor_t::on_postprocess_io);
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_start");
    auto my_url = peer_state.get_url();
    if (peer_state < peer->get_state()) {
        auto other_url = peer->get_state().get_url();
        LOG_DEBUG(log, "there is a better connection ({}) to peer than me ({}), shut self down", my_url, other_url);
        return do_shutdown();
    }

    auto stack_ctx = stack_context_t(*this);
    send<payload::controller_up_t>(coordinator, address, my_url->clone(), peer->device_id(), outgoing_buffer);
    send_cluster_config(stack_ctx);
    resources->acquire(resource::peer);
    LOG_INFO(log, "is online (connection: {})", my_url);
    announced = true;
}

void controller_actor_t::shutdown_start() noexcept {
    auto fs_requests = resources->has(resource::fs);
    LOG_TRACE(log, "shutdown_start, ongoing fs requests = {}, announced = {}", fs_requests, announced);
    send<payload::controller_predown_t>(coordinator, address, peer_address, shutdown_reason, announced);
    if (fs_requests) {
        fs_ack_timer = start_timer(shutdown_timeout * 8 / 9, *this, &controller_actor_t::on_fs_ack_timer);
    }
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish, blocks_requested = {}", rx_blocks_requested);
    peer->release_iterator(file_iterator);
    file_iterator.reset();
    synchronizing_folders.clear();
    postponed_files.clear();
    synchronizing_files.clear();
    if (announced) {
        send<payload::controller_down_t>(coordinator, address, peer_address);
    }
    r::actor_base_t::shutdown_finish();
}

void controller_actor_t::send_cluster_config(stack_context_t &ctx) noexcept {
    LOG_TRACE(log, "sending cluster config");
    auto cluster_config = cluster->generate(*peer);
    auto bytes = proto::serialize(cluster_config, peer->get_compression());
    ctx.push(std::move(bytes));
    send_new_indices();
}

void controller_actor_t::send_new_indices() noexcept {
    if (updates_streamer && peer_address) {
        auto &remote_views = peer->get_remote_view_map();
        for (auto it : cluster->get_folders()) {
            auto &folder = *it.item;
            auto peer_folder = folder.is_shared_with(*peer);
            if (peer_folder) {
                auto &local_device = *cluster->get_device();
                auto local_folder = folder.get_folder_infos().by_device(local_device);
                auto local_sha256 = local_device.device_id().get_sha256();
                auto remote_view = remote_views.get(local_sha256, folder.get_id());
                if (remote_view) {
                    if (remote_view->index_id != local_folder->get_index()) {
                        LOG_DEBUG(log, "peer still has wrong index for '{}' ({:#x} vs {:#x}), refreshing",
                                  folder.get_id(), remote_view->index_id, local_folder->get_index());
                        updates_streamer->on_remote_refresh();
                    }
                }
            }
        }
    }
}

void controller_actor_t::on_tx_signal(message::tx_signal_t &) noexcept {
    LOG_TRACE(log, "on_tx_signal, outgoing buff size is {} bytes", *outgoing_buffer);
    auto stack_ctx = stack_context_t(*this);
    push_pending(stack_ctx);
}

void controller_actor_t::on_peer_down(message::peer_down_t &message) noexcept {
    if (message.payload.peer == peer_address) {
        if (resources->has(resource::peer)) {
            resources->release(resource::peer);
            peer_address.reset();
        }
        auto &ee = message.payload.ee;
        LOG_TRACE(log, "on_peer_down reason: {}", ee->message());
        do_shutdown(ee);
    }
}

void controller_actor_t::on_postprocess_io(fs::message::io_commands_t &message) noexcept {
    auto stack_ctx = stack_context_t(*this);
    resources->release(resource::fs);
    for (auto &cmd : message.payload.commands) {
        std::visit(
            [&](auto &cmd) {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::is_same_v<T, fs::payload::block_request_t>) {
                    postprocess_io(cmd, stack_ctx);
                } else {
                    if constexpr (std::is_same_v<T, fs::payload::append_block_t>) {
                        cluster->modify_write_requests(+1);
                    }
                    if (cmd.result.has_error()) {
                        auto &ec = cmd.result.assume_error();
                        LOG_ERROR(log, "i/o error (postprocessing): {}", ec.message());
                        do_shutdown(make_error(ec));
                    } else {
                        postprocess_io(cmd, stack_ctx);
                    }
                }
            },
            cmd);
    }
    pull_next(stack_ctx);
}

void controller_actor_t::push_pending(stack_context_t &ctx) noexcept {
    struct update_t {
        model::folder_info_t *folder;
        proto::IndexUpdate index;
        bool first;
    };
    using updates_t = std::vector<update_t>;

    if (!updates_streamer || !peer_address) {
        return;
    }

    auto updates = updates_t{};
    auto get_update = [&](model::file_info_t &file, model::folder_info_t &folder_info) -> update_t & {
        for (auto &p : updates) {
            if (p.folder == &folder_info) {
                return p;
            }
        }
        auto &update = updates.emplace_back(&folder_info, proto::IndexUpdate(), false);
        proto::set_folder(update.index, folder_info.get_folder()->get_id());
        return update;
    };

    auto expected_sz = std::uint32_t(0);
    while (expected_sz < outgoing_buffer_max - *outgoing_buffer) {
        auto [file_info, folder_info, first] = updates_streamer->next();
        if (file_info) {
            expected_sz += file_info->expected_meta_size();
            auto &update = get_update(*file_info, *folder_info);
            proto::add_files(update.index, file_info->as_proto(true));
            LOG_TRACE(log, "pushing index update for: '{}', seq = {}", *file_info, file_info->get_sequence());
            if (first) {
                update.first = true;
                LOG_TRACE(log, "(upgraded to Index)");
            }
        } else {
            break;
        }
    }

    auto compression = peer->get_compression();
    for (auto &u : updates) {
        auto &index = u.index;
        if (proto::get_files_size(index) > 0) {
            auto data = utils::bytes_t();
            if (u.first) {
                data = proto::serialize(proto::convert(std::move(index)));
            } else {
                data = proto::serialize(index, compression);
            }
            ctx.push(std::move(data));
        }
    }
}

void controller_actor_t::pull_next(stack_context_t &context) noexcept {
    if (!file_iterator) {
        return;
    }
    LOG_TRACE(log, "pull_next (pull_signal_t), blocks requested = {}, request pool = {}, cluster write reqs = {}",
              rx_blocks_requested, request_pool, cluster->get_write_requests());
    auto advances = std::uint_fast32_t{0};
    auto can_pull_more = [&]() -> bool {
        bool ignore = (rx_blocks_requested >= blocks_max_requested || request_pool < 0) // rx buff is going to be full
                      || (state != r::state_t::OPERATIONAL) // request pool sz = 32505856e are shutting down
                      || !cluster->get_write_requests() || advances > advances_per_iteration;
        return !ignore;
    };

    using file_set_t = std::pmr::set<model::file_info_t *>;
    using allocator_t = std::pmr::polymorphic_allocator<char>;

    auto buffer = std::array<std::byte, 1024 * 16>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = allocator_t(&pool);
    auto checked_children = file_set_t(allocator);

    auto seen_files = file_set_t();
OUTER:
    while (can_pull_more()) {
        if (block_iterator) {
            auto file = block_iterator->get_source();
            seen_files.emplace(file);
            if (*block_iterator) {
                auto file_block = block_iterator->next();
                auto fi = &block_iterator->get_source_folder();
                auto block = file_block.block();
                if (!file_block.block()->is_locked()) {
                    preprocess_block(file_block, *fi, context);
                } else {
                    auto b = model::block_info_ptr_t(const_cast<model::block_info_t *>(block));
                    auto f = model::file_info_ptr_t(file_block.file());
                    block_2_files.emplace(block_2_file_t{std::move(b), std::move(f)});
                }
                continue;
            } else {
                // file_iterator->commit_sync(block_iterator->get_source());
                block_iterator.reset();
            }
            continue;
        }
        if (!block_iterator && !postponed_files.empty()) {
            for (auto it = postponed_files.begin(); it != postponed_files.end();) {
                auto file = std::move(*it);
                it = postponed_files.erase(it);
                if (seen_files.count(file.get())) {
                    continue;
                }
                auto folder_uuid = file->get_folder_uuid();
                auto fi = (model::folder_info_t *)(nullptr);
                for (auto fit : cluster->get_folders()) {
                    fi = fit.item->get_folder_infos().by_uuid(folder_uuid).get();
                    if (fi) {
                        break;
                    }
                }
                if (fi) {
                    auto bi = model::block_iterator_ptr_t();
                    bi = new model::blocks_iterator_t(*file, *fi);
                    if (bi) {
                        block_iterator = bi;
                        goto OUTER;
                    }
                }
            }
        }
        if (auto [file, peer_folder, action] = file_iterator->next(); action != model::advance_action_t::ignore) {
            auto in_sync = synchronizing_files.count(file->get_full_id());
            if (in_sync) {
                continue;
            }
            auto augmentation = file->get_augmentation();
            auto presence = static_cast<presentation::presence_t *>(augmentation.get());
            if (!presence->is_unique()) {
                LOG_WARN(log, "file '{}' is not unique in folder '{}', skipping", file->get_name()->get_full_name(),
                         peer_folder->get_folder()->get_label());
                continue;
            }
            if (file->is_locally_available()) {
                auto finalize = false;
                auto &peer_f = const_cast<model::folder_info_t &>(*peer_folder);
                if (is_unflushed(file, peer_f)) {
                    LOG_DEBUG(log, "finalizing unfinished file '{}'", file->get_name()->get_full_name());
                    auto guard = file->guard(*peer_folder);
                    synchronizing_files[file->get_full_id()] = std::move(guard);
                    auto &folder_infos = peer_f.get_folder()->get_folder_infos();
                    auto &local_files = folder_infos.by_device(*cluster->get_device())->get_file_infos();
                    auto filename = file->get_name()->get_full_name();
                    auto local_file = local_files.by_name(filename);
                    io_finish_file(local_file.get(), *file, peer_f, action, context);
                } else {
                    io_advance(action, *file, peer_f, context);
                }
                ++advances;
            } else if (file->get_size()) {
                auto bi = model::block_iterator_ptr_t();
                bi = new model::blocks_iterator_t(*file, *peer_folder);
                if (*bi) {
                    block_iterator = bi;
                    auto guard = file->guard(*peer_folder);
                    synchronizing_files[file->get_full_id()] = std::move(guard);
                }
            }

            continue;
        }
        break;
    }
}

void controller_actor_t::io_advance(model::advance_action_t action, model::file_info_t &peer_file,
                                    model::folder_info_t &peer_folder, stack_context_t &ctx) {
    using namespace model::diff::advance;
    LOG_TRACE(log, "going to advance (action: {}) on file '{}'", static_cast<int>(action), peer_file);
    assert((action == model::advance_action_t::remote_copy) || (action == model::advance_action_t::resolve_remote_win));

    auto conflict_path = bfs::path();
    if (action == model::advance_action_t::resolve_remote_win) {
        auto local_folder = peer_folder.get_folder()->get_folder_infos().by_device(*cluster->get_device());
        auto local_file = local_folder->get_file_infos().by_name(peer_file.get_name()->get_full_name());
        conflict_path = peer_folder.get_folder()->get_path() / bfs::path(local_file->make_conflicting_name());
    }
    auto path = peer_file.get_path(peer_folder);
    auto type = model::file_info_t::as_type(peer_file.get_type());
    auto size = peer_file.get_size();
    auto modified = peer_file.get_modified_s();
    auto target = std::string(peer_file.get_link_target());
    auto deleted = peer_file.is_deleted();
    auto perms = peer_file.get_permissions();
    bool no_permissions = !utils::platform_t::permissions_supported(path) ||
                          peer_folder.get_folder()->are_permissions_ignored() || peer_file.has_no_permissions();

    auto context = fs::payload::extendended_context_prt_t{};
    context.reset(new remote_copy_context_t(action, peer_file, peer_folder));
    auto payload = fs::payload::remote_copy_t(std::move(context), path, conflict_path, type, size, perms, modified,
                                              target, deleted, no_permissions);
    ctx.push(std::move(payload));
}

void controller_actor_t::io_append_block(model::file_info_t &peer_file, model::folder_info_t &peer_folder,
                                         uint32_t block_index, utils::bytes_t data, stack_context_t &ctx) {
    auto path = peer_file.get_path(peer_folder);
    auto file_size = peer_file.get_size();
    auto block = const_cast<model::block_info_t *>(peer_file.iterate_blocks(block_index).next());
    auto offset = peer_file.get_block_offset(block_index);
    auto context = fs::payload::extendended_context_prt_t{};
    context.reset(new block_ack_context_t(block, peer_file, peer_folder, block_index));
    auto payload = fs::payload::append_block_t(std::move(context), path, std::move(data), offset, file_size);
    ctx.push(std::move(payload));
}

void controller_actor_t::io_clone_block(const model::file_block_t &file_block, model::folder_info_t &target_fi,
                                        stack_context_t &ctx) {
    auto src = (const model::file_info_t *)(nullptr);
    auto target = const_cast<model::file_info_t *>(file_block.file());
    auto it = file_block.block()->iterate_blocks();
    auto src_block_index = std::uint32_t(0);
    auto src_fi = (const model::folder_info_t *)(nullptr);
    auto target_block_index = file_block.block_index();
    while (auto b = it.next()) {
        if (b->is_locally_available()) {
            src_block_index = b->block_index();
            src = b->file();
            auto folder_uuid = src->get_folder_uuid();
            for (auto &folder_it : cluster->get_folders()) {
                if (auto fi = folder_it.item->get_folder_infos().by_uuid(folder_uuid)) {
                    src_fi = fi.get();
                    break;
                }
            }
            if (src_fi) {
                break;
            }
        }
    }
    assert(src_fi);

    auto source_path = src->get_path(*src_fi);
    auto source_offset = src->get_block_offset(src_block_index);
    auto target_path = target->get_path(target_fi);
    auto target_offset = target->get_block_offset(target_block_index);
    auto target_sz = target->get_size();
    auto block_sz = src->iterate_blocks(src_block_index).next()->get_size();
    auto context = fs::payload::extendended_context_prt_t{};
    auto block = const_cast<model::block_info_t *>(file_block.block());
    context.reset(new block_ack_context_t(block, *target, target_fi, target_block_index));
    auto payload = fs::payload::clone_block_t(std::move(context), target_path, target_offset, target_sz, source_path,
                                              source_offset, block_sz);
    ctx.push(std::move(payload));
}

void controller_actor_t::io_finish_file(model::file_info_t *local_file, model::file_info_t &peer_file,
                                        model::folder_info_t &peer_folder, model::advance_action_t action,
                                        stack_context_t &ctx) {
    assert((action == model::advance_action_t::remote_copy) || (action == model::advance_action_t::resolve_remote_win));
    auto path = peer_file.get_path(peer_folder);
    auto conflict_path = bfs::path();
    auto file_size = peer_file.get_size();
    auto modified_s = peer_file.get_modified_s();
    if (action == model::advance_action_t::resolve_remote_win) {
        assert(local_file);
        conflict_path = peer_folder.get_folder()->get_path() / bfs::path(local_file->make_conflicting_name());
    }
    auto perms = peer_file.get_permissions();
    bool no_permissions = !utils::platform_t::permissions_supported(path) ||
                          peer_folder.get_folder()->are_permissions_ignored() || peer_file.has_no_permissions();

    auto context = fs::payload::extendended_context_prt_t{};
    context.reset(new finish_file_context_t(peer_file, peer_folder, action));
    auto payload = fs::payload::finish_file_t(std::move(context), std::move(path), std::move(conflict_path), file_size,
                                              modified_s, perms, no_permissions);
    ctx.push(std::move(payload));
}

auto controller_actor_t::io_make_request_block(model::file_info_t &source, model::folder_info_t &source_fi,
                                               proto::Request req) -> fs::payload::io_command_t {
    auto path = source.get_path(source_fi);
    auto offset = proto::get_offset(req);
    auto block_size = proto::get_size(req);
    auto context = fs::payload::extendended_context_prt_t{};
    context.reset(new block_request_context_t(std::move(req)));
    auto payload = fs::payload::block_request_t(std::move(context), std::move(path), offset, block_size);
    return payload;
}

void controller_actor_t::preprocess_block(model::file_block_t &file_block, const model::folder_info_t &source_folder,
                                          stack_context_t &ctx) noexcept {
    using namespace model::diff;
    if (!peer_address) {
        LOG_TRACE(log, "ignoring block, as there is no peer");
        return;
    }

    auto file = file_block.file();
    auto block = file_block.block();
    acquire_block(file_block, source_folder, ctx);

    auto hash = block->get_hash();
    auto last_index = file->iterate_blocks(0).get_total() - 1;
    if (file_block.is_locally_available()) {
        LOG_TRACE(log, "cloning locally available block '{}', file = {}, block index = {} / {}", hash, *file,
                  file_block.block_index(), last_index);
        auto &folder_infos = source_folder.get_folder()->get_folder_infos();
        auto target_fi = folder_infos.by_uuid(file->get_folder_uuid());
        io_clone_block(file_block, *target_fi, ctx);
    } else {
        auto request_id = block_requests_next;
        for (std::uint_fast32_t i = 0; i < blocks_max_requested; ++i) {
            if (!block_requests[request_id]) {
                if (request_id + 1 >= blocks_max_requested) {
                    block_requests_next = 0;
                } else {
                    ++block_requests_next;
                }
                break;
            } else {
                ++request_id;
                if (request_id >= blocks_max_requested) {
                    request_id = 0;
                }
            }
        }
        assert(!block_requests[request_id]);

        auto sz = block->get_size();
        LOG_TRACE(log, "requesting block '{}', file '{}', index = {} of {} ({} bytes), pool sz = {}, req.id = {}", hash,
                  *file, file_block.block_index(), last_index, sz, request_pool, request_id);

        proto::Request req;
        proto::set_id(req, static_cast<std::int32_t>(request_id));
        proto::set_folder(req, source_folder.get_folder()->get_id());
        proto::set_name(req, file->get_name()->get_full_name());
        proto::set_offset(req, static_cast<std::int32_t>(file_block.get_offset()));
        proto::set_size(req, static_cast<std::int32_t>(block->get_size()));
        proto::set_hash(req, block->get_hash());

        ctx.push(proto::serialize(req, peer->get_compression()));

        auto context = fs::payload::extendended_context_prt_t{};
        context.reset(new peer_request_context_t(std::move(req), file->get_sequence(), file_block.block_index()));
        block_requests[request_id] = std::move(context);
        ++rx_blocks_requested;
        request_pool -= (int64_t)sz;
    }
}

void controller_actor_t::on_forward(message::forwarded_messages_t &message) noexcept {
    if (state != r::state_t::OPERATIONAL) {
        return;
    }
    auto stack_ctx = stack_context_t(*this);
    for (auto &bep_msg : message.payload) {
        std::visit([&](auto &msg) { on_message(msg, stack_ctx); }, bep_msg);
    }
}

void controller_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    auto custom = const_cast<void *>(message.payload.custom);
    auto ctx = update_context_t(*this, custom == this, false);
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, &ctx);
    if (!r) {
        auto ee = make_error(r.assume_error());
        return do_shutdown(ee);
    }
    pull_next(ctx);
    push_pending(ctx);
}

auto controller_actor_t::operator()(const model::diff::peer::cluster_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<update_context_t *>(custom);
    auto result = diff.visit_next(*this, custom);
    if (ctx->from_self && !result.has_error()) {
        updates_streamer.reset(new model::updates_streamer_t(*cluster, *peer));
        send_new_indices();
        if (!file_iterator) {
            file_iterator = peer->create_iterator(*cluster);
        }
    }
    return result;
}

auto controller_actor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<update_context_t *>(custom);
    if (ctx->from_self) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto &files_map = folder->get_folder_infos().by_device(*peer)->get_file_infos();
        for (auto &f : diff.files) {
            auto file = files_map.by_name(proto::get_name(f));
            cancel_sync(file.get());
        }
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto ctx = reinterpret_cast<update_context_t *>(custom);
    if (folder && !folder->is_suspended()) {
        auto &folder_infos = folder->get_folder_infos();
        auto &self = *cluster->get_device();
        auto local_folder = folder_infos.by_device(self);
        auto peer_folder = folder_infos.by_device_id(diff.peer_id);
        auto local_file = model::file_info_ptr_t();
        if (peer_folder) {
            auto name = proto::get_name(diff.proto_source);
            auto file = peer_folder->get_file_infos().by_name(name);
            if (file) {
                if (diff.peer_id == peer->device_id().get_sha256()) {
                    local_file = local_folder->get_file_infos().by_name(name);
                    if (file->is_file() && file->iterate_blocks().get_total()) {
                        cancel_sync(file.get());
                    }
                }
            }
        }
        if (diff.peer_id == self.device_id().get_sha256()) {
            auto name = proto::get_name(diff.proto_local);
            local_file = local_folder->get_file_infos().by_name(name);
            assert(local_file);
        }
        if (local_file) {
            if (updates_streamer) {
                updates_streamer->on_update(*local_file, *local_folder);
            }
        }
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.peer_id == peer->device_id().get_sha256()) {
        if (peer_state < diff.state) {
            auto my_url = peer_state.get_url();
            auto other_url = diff.state.get_url();
            LOG_DEBUG(log, "there is a better connection ({}) to peer than me ({}), shut self down", my_url, other_url);
            do_shutdown();
        }
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<update_context_t *>(custom);
    auto folder = cluster->get_folders().by_id(db::get_id(diff.db));
    if (folder->is_shared_with(*peer)) {
        if (updates_streamer) {
            updates_streamer->on_remote_refresh();
        }
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto process = diff.device_id == peer->device_id().get_sha256();
    if (process) {
        auto ctx = reinterpret_cast<update_context_t *>(custom);
        if (!ctx->cluster_config_sent) {
            ctx->cluster_config_sent = true;
            send_cluster_config(*ctx);
        }
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::remove_files_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.device_id == peer->device_id().get_sha256()) {
        auto requesting_file = block_iterator ? block_iterator->get_source() : nullptr;
        for (auto &key : diff.keys) {
            auto full_id = utils::bytes_view_t(key).subspan(1);
            if (requesting_file) {
                if (requesting_file->get_full_id() == full_id) {
                    block_iterator.reset();
                    requesting_file = nullptr;
                }
            }
#if 0
            if (auto it = postponed_files.find(full_id); it != postponed_files.end()) {
                postponed_files.erase(it);
            }
#endif
            if (auto it = synchronizing_files.find(full_id); it != synchronizing_files.end()) {
                it->second.forget(); // don't care about unlocking as the file is removed anyway
                synchronizing_files.erase(it);
            }
        }
    }

    return diff.visit_next(*this, custom);
}
auto controller_actor_t::operator()(const model::diff::modify::remove_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    for (auto &key : diff.keys) {
        auto decomposed = model::folder_info_t::decompose_key(key);
        auto device_key = decomposed.device_key();
        if (device_key == peer->get_key() || device_key == cluster->get_device()->get_key()) {
            auto ctx = reinterpret_cast<update_context_t *>(custom);
            if (!ctx->cluster_config_sent) {
                ctx->cluster_config_sent = true;
                send_cluster_config(*ctx);
            }
            break;
        }
    }

    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::mark_reachable_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto ctx = reinterpret_cast<update_context_t *>(custom);
    auto &folder_id = diff.folder_id;
    auto &file_name = diff.file_name;
    auto folder = cluster->get_folders().by_id(folder_id);
    auto &folder_infos = folder->get_folder_infos();
    auto device = cluster->get_devices().by_sha256(diff.device_id);
    auto folder_info = folder_infos.by_device(*device);
    auto file = folder_info->get_file_infos().by_name(file_name);
    if (ctx->from_self) {
        cancel_sync(file.get());
    }
    if (device == cluster->get_device() && updates_streamer) {
        updates_streamer->on_update(*file, *folder_info);
    }
    return diff.visit_next(*this, custom);
}

auto controller_actor_t::operator()(const model::diff::modify::block_ack_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (diff.device_id == peer->device_id().get_sha256()) {
        auto ctx = reinterpret_cast<update_context_t *>(custom);
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        if (folder) {
            auto &folder_infos = folder->get_folder_infos();
            auto folder_info = folder_infos.by_device_id(diff.device_id);
            if (folder_info) {
                auto file = folder_info->get_file_infos().by_name(diff.file_name);
                if (file) {
                    if (file->is_locally_available()) {
                        auto local_fi = folder_infos.by_device(*cluster->get_device());
                        if (local_fi) {
                            auto it = synchronizing_files.find(file->get_full_id());
                            if (it != synchronizing_files.end()) {
                                auto &guard = it->second;
                                if (!guard.finished) {
                                    guard.finished = true;
                                    auto local_file = local_fi->get_file_infos().by_name(diff.file_name);
                                    auto action = resolve(*file, local_file.get(), *local_fi);
                                    if (action != model::advance_action_t::ignore) {
                                        LOG_TRACE(log, "on_block_update, finalizing '{}'", *file);
                                        io_finish_file(local_file.get(), *file, *folder_info, action, *ctx);
                                    } else {
                                        LOG_DEBUG(log, "on_block_update, already have actual '{}', noop", *file);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        release_block(diff.folder_id, diff.block_hash, *ctx);
    }

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

void controller_actor_t::on_message(proto::ClusterConfig &message, stack_context_t &ctx) noexcept {
    using diff_t = model::diff::peer::cluster_update_t;
    LOG_DEBUG(log, "on_message (ClusterConfig)");
    auto diff_opt = diff_t::create(default_path, *cluster, *sequencer, *peer, message);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    ctx.push(std::move(diff_opt).assume_value());
}

void controller_actor_t::on_message(proto::Index &msg, stack_context_t &ctx) noexcept {
    LOG_DEBUG(log, "on_message (Index)");
    auto diff_opt = model::diff::peer::update_folder_t::create(*cluster, *sequencer, *peer, msg);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &diff = diff_opt.assume_value();
    auto folder = cluster->get_folders().by_id(proto::get_folder(msg));
    auto file_count = proto::get_files_size(msg);
    LOG_DEBUG(log, "on_message (Index), folder = {}, files = {}", folder->get_label(), file_count);
    ctx.push(std::move(diff_opt).assume_value());
}

void controller_actor_t::on_message(proto::IndexUpdate &msg, stack_context_t &ctx) noexcept {
    LOG_TRACE(log, "on_message (IndexUpdate)");
    auto diff_opt = model::diff::peer::update_folder_t::create(*cluster, *sequencer, *peer, msg);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    auto &diff = diff_opt.assume_value();
    auto folder = cluster->get_folders().by_id(proto::get_folder(msg));
    auto file_count = proto::get_files_size(msg);
    LOG_DEBUG(log, "on_message (IndexUpdate), folder = {}, files = {}", folder->get_label(), file_count);
    ctx.push(std::move(diff_opt).assume_value());
}

void controller_actor_t::on_message(proto::Request &req, stack_context_t &ctx) noexcept {
    proto::Response res;
    auto code = proto::ErrorCode::NO_BEP_ERROR;
    auto forward = true;

    auto folder_id = proto::get_folder(req);
    auto folder = cluster->get_folders().by_id(folder_id);
    auto local_folder = model::folder_info_ptr_t();
    auto local_file = model::file_info_ptr_t();
    if (!folder) {
        code = proto::ErrorCode::NO_SUCH_FILE;
    } else {
        auto &folder_infos = folder->get_folder_infos();
        if (folder->is_suspended()) {
            LOG_WARN(log, "folder '{}' is suspended for requests", folder->get_label());
            code = proto::ErrorCode::GENERIC;
        } else {
            auto peer_folder = folder_infos.by_device(*peer);
            if (!peer_folder) {
                code = proto::ErrorCode::NO_SUCH_FILE;
            } else {
                local_folder = folder_infos.by_device(*cluster->get_device());
                auto name = proto::get_name(req);
                local_file = local_folder->get_file_infos().by_name(name);
                if (!local_file) {
                    code = proto::ErrorCode::NO_SUCH_FILE;
                } else {
                    if (!local_file->is_file()) {
                        LOG_WARN(log, "attempt to request non-regular file: {}", *local_file);
                        code = proto::ErrorCode::GENERIC;
                    } else {
                        if (tx_blocks_requested > blocks_max_requested * constants::tx_blocks_max_factor) {
                            LOG_DEBUG(log, "peer requesting too many blocks ({}), enqueuing...", tx_blocks_requested);
                            forward = false;
                        }
                    }
                }
            }
        }
    }

    if (code != proto::ErrorCode::NO_BEP_ERROR) {
        if (peer_address) {
            proto::set_id(res, proto::get_id(req));
            proto::set_code(res, code);
            auto data = proto::serialize(res, peer->get_compression());
            ctx.push(std::move(data));
        }
    } else {
        auto io_command = io_make_request_block(*local_file, *local_folder, std::move(req));
        if (forward) {
            ++tx_blocks_requested;
            ctx.push(std::move(io_command));
        } else {
            block_read_queue.emplace_back(std::move(io_command));
        }
    }
}

void controller_actor_t::on_message(proto::Response &message, stack_context_t &ctx) noexcept {
    --rx_blocks_requested;

    auto request_id = proto::get_id(message);
    if (request_id > static_cast<std::int32_t>(block_requests.size()) || request_id < 0) {
        LOG_WARN(log, "responce id = {} is incorrect, shut self down", request_id);
        return do_shutdown();
    }
    auto request_context = std::move(block_requests[request_id]);
    if (!request_context) {
        LOG_WARN(log, "there was no request for responce id = {}, shut self down", request_id);
        return do_shutdown();
    }
    auto peer_context = static_cast<peer_request_context_t *>(request_context.get());
    block_requests_next = request_id;

    auto block_hash = proto::get_hash(peer_context->request);
    auto folder_id = proto::get_folder(peer_context->request);
    auto file_name = proto::get_name(peer_context->request);
    auto folder = cluster->get_folders().by_id(folder_id);
    auto peer_folder = model::folder_info_ptr_t();
    auto file = model::file_info_ptr_t();
    auto block = (const model::block_info_t *)(nullptr);
    auto file_block = (const model::file_block_t *)(nullptr);

    if (folder && !folder->is_suspended()) {
        peer_folder = folder->get_folder_infos().by_device(*peer);
    }
    if (peer_folder) {
        auto f = peer_folder->get_file_infos().by_name(file_name);
        if (f->get_sequence() == peer_context->sequence) {
            file = std::move(f);
        }
    }
    if (file) {
        auto b = file->iterate_blocks(peer_context->block_index).next();
        auto it = b->iterate_blocks();
        while (auto fb = it.next()) {
            if (fb->file() == file) {
                file_block = fb;
                block = b;
                break;
            }
        }
    }
    if (!file_block) {
        file = {};
    }

    bool try_next = true;
    bool do_release_block = false;
    auto stack_ctx = stack_context_t(*this);
    if (state != r::state_t::OPERATIONAL) {
        do_release_block = true;
        try_next = false;
        LOG_DEBUG(log, "on_block, file = '{}',  non-operational ignoring", file_name);
    } else if (!file) {
        LOG_DEBUG(log, "on_block, file '{}' is not longer available in '{}'", file_name, folder_id);
        do_release_block = true;
    } else {
        auto code = proto::get_code(message);
        auto code_int = (int)code;
        if (code_int) {
            do_release_block = true;
            if (!file->is_unreachable()) {
                LOG_WARN(log, "can't receive block from file '{}': {}; marking unreachable", *file, code_int);
                file->mark_unreachable(true);
                stack_ctx.push(new model::diff::modify::mark_reachable_t(*file, *peer_folder, false));
                cancel_sync(file.get());
            }
        } else {
            auto data = utils::bytes_t(proto::extract_data(message));
            // auto hash = file_block->block()->get_hash();
            // auto hash_bytes = utils::bytes_t(hash.begin(), hash.end());
            request_pool += block->get_size();

            hasher->calc_digest(std::move(data), file_block->block_index(), address, std::move(request_context));
            resources->acquire(resource::hash);
        }
    }
    if (try_next) {
        pull_next(ctx);
    }
    if (do_release_block) {
        release_block(folder_id, block_hash, ctx);
    }
}

static inline void ack_block(block_ack_context_t *io_ctx, model::cluster_t *cluster,
                             controller_actor_t::stack_context_t &ctx) noexcept {
    using namespace model::diff;
    auto folder_id = io_ctx->folder->get_id();
    auto diff = cluster_diff_ptr_t();
    auto name = std::string(io_ctx->target_file->get_name()->get_full_name());
    auto device_id = utils::bytes_t(io_ctx->target_folder->get_device()->device_id().get_sha256());
    auto hash = utils::bytes_t(io_ctx->block->get_hash());
    diff.reset(new modify::block_ack_t(std::move(name), std::string(folder_id), std::move(device_id), std::move(hash),
                                       io_ctx->block_index));
    ctx.push(std::move(diff));
}

void controller_actor_t::postprocess_io(fs::payload::block_request_t &res, stack_context_t &ctx) noexcept {
    --tx_blocks_requested;
    if (!peer_address) {
        return;
    }

    auto io_ctx = static_cast<block_request_context_t *>(res.context.get());
    proto::Response reply;
    proto::set_id(reply, proto::get_id(io_ctx->request));
    if (res.result.has_error()) {
        proto::set_code(reply, proto::ErrorCode::GENERIC);
    } else {
        auto &data = res.result.assume_value();
        proto::set_data(reply, std::move(data));
    }

    auto data = proto::serialize(reply, peer->get_compression());
    ctx.push(std::move(data));
}

void controller_actor_t::postprocess_io(fs::payload::remote_copy_t &res, stack_context_t &ctx) noexcept {
    using namespace model::diff::advance;
    assert(res.result);
    auto io_ctx = static_cast<remote_copy_context_t *>(res.context.get());
    auto diff = advance_t::create(io_ctx->action, *io_ctx->peer_file, *io_ctx->peer_folder, *sequencer);
    ctx.push(std::move(diff));
}

void controller_actor_t::postprocess_io(fs::payload::append_block_t &res, stack_context_t &ctx) noexcept {
    auto io_ctx = static_cast<block_ack_context_t *>(res.context.get());
    ack_block(io_ctx, cluster.get(), ctx);
}

void controller_actor_t::postprocess_io(fs::payload::clone_block_t &res, stack_context_t &ctx) noexcept {
    auto io_ctx = static_cast<block_ack_context_t *>(res.context.get());
    ack_block(io_ctx, cluster.get(), ctx);
}

void controller_actor_t::postprocess_io(fs::payload::finish_file_t &res, stack_context_t &ctx) noexcept {
    using namespace model::diff;
    auto io_ctx = static_cast<finish_file_context_t *>(res.context.get());

    auto diff = advance::advance_t::create(io_ctx->action, *io_ctx->peer_file, *io_ctx->peer_folder, *sequencer);
    ctx.push(std::move(diff));
}

void controller_actor_t::on_digest(hasher::message::digest_t &res) noexcept {
    using namespace model::diff;
    resources->release(resource::hash);

    auto peer_context = static_cast<peer_request_context_t *>(res.payload.context.get());

    auto block_hash = proto::get_hash(peer_context->request);
    auto folder_id = proto::get_folder(peer_context->request);
    auto file_name = proto::get_name(peer_context->request);
    auto folder = cluster->get_folders().by_id(folder_id);
    auto peer_folder = model::folder_info_ptr_t();
    auto file = model::file_info_ptr_t();
    auto block = (const model::block_info_t *)(nullptr);
    auto file_block = (const model::file_block_t *)(nullptr);

    if (folder && !folder->is_suspended()) {
        peer_folder = folder->get_folder_infos().by_device(*peer);
    }
    if (peer_folder) {
        auto f = peer_folder->get_file_infos().by_name(file_name);
        if (f->get_sequence() == peer_context->sequence) {
            file = std::move(f);
        }
    }
    if (file) {
        auto b = file->iterate_blocks(peer_context->block_index).next();
        auto it = b->iterate_blocks();
        while (auto fb = it.next()) {
            if (fb->file() == file) {
                file_block = fb;
                block = b;
                break;
            }
        }
    }
    if (!file_block) {
        file = {};
    }
    bool do_release_block = false;
    bool try_next = false;
    auto stack_ctx = stack_context_t(*this);
    auto &result = res.payload.result;
    if (state != r::state_t::OPERATIONAL) {
        LOG_DEBUG(log, "on_validation, non-operational, ingoring block from '{}'", file_name);
        do_release_block = true;
    } else if (!file) {
        LOG_DEBUG(log, "on_validation, file '{}' is not longer available in '{}'", file_name, folder_id);
        do_release_block = true;
    } else if (!peer_address) {
        LOG_DEBUG(log, "on_validation, file: '{}', peer is no longer available", file_name);
        do_release_block = true;
    } else {
        if (result.has_error() || result.assume_value() != block->get_hash()) {
            if (!file->is_unreachable()) {
                auto ec = utils::make_error_code(utils::protocol_error_code_t::digest_mismatch);
                LOG_WARN(log, "digest mismatch for file '{}', expected = {}; marking unreachable", *file,
                         block->get_hash());
                file->mark_unreachable(true);
                stack_ctx.push(new model::diff::modify::mark_reachable_t(*file, *peer_folder, false));
            }
            do_release_block = true;
            try_next = true;
        } else {
            auto &data = res.payload.data;
            auto index = file_block->block_index();
            auto already_have = file_block->is_locally_available();
            LOG_TRACE(log, "{}, got block {}, already have: {}, write requests left = {}", *file, index,
                      already_have ? "y" : "n", cluster->get_write_requests());
            if (already_have) {
                try_next = true;
                do_release_block = true;
            } else {
                io_append_block(*file, *peer_folder, index, std::move(data), stack_ctx);
            }
        }
    }
    if (do_release_block) {
        release_block(folder_id, block_hash, stack_ctx);
        if (file) {
            cancel_sync(file.get());
        }
    }
    if (try_next) {
        pull_next(stack_ctx);
    }
}

void controller_actor_t::on_fs_predown(message::fs_predown_t &message) noexcept {
    auto count = resources->has(resource::fs);
    LOG_DEBUG(log, "on_fs_predown, has fs requests = {}", count);
    do_shutdown();
}

void controller_actor_t::on_fs_ack_timer(r::request_id_t, bool cancelled) noexcept {
    if (!cancelled) {
        auto count = resources->has(resource::fs);
        if (count) {
            LOG_WARN(log, "forgetting {} fs requests", count);
            while (resources->has(resource::fs)) {
                resources->release(resource::fs);
            }
            cluster->modify_write_requests(static_cast<int32_t>(count));
        }
    }
}

auto controller_actor_t::get_sync_info(model::folder_t *folder) noexcept -> folder_synchronization_t & {
    auto it = synchronizing_folders.find(folder);
    if (it == synchronizing_folders.end()) {
        auto pair = synchronizing_folders.emplace(folder, folder_synchronization_t(*this, *folder));
        return pair.first->second;
    }
    return it->second;
}

auto controller_actor_t::get_sync_info(std::string_view folder_id) noexcept -> folder_synchronization_t & {
    auto predicate = [folder_id](const auto &it) -> bool { return it.first->get_id() == folder_id; };
    auto it = std::find_if(synchronizing_folders.begin(), synchronizing_folders.end(), predicate);
    assert(it != synchronizing_folders.end());
    return it->second;
}

void controller_actor_t::acquire_block(const model::file_block_t &file_block, const model::folder_info_t &folder_info,
                                       stack_context_t &context) noexcept {
    auto block = file_block.block();
    auto folder = folder_info.get_folder();
    LOG_TRACE(log, "acquire block '{}', {}", block->get_hash(), (const void *)block);
    get_sync_info(folder).start_fetching(const_cast<model::block_info_t *>(block), context);
}

void controller_actor_t::release_block(std::string_view folder_id, utils::bytes_view_t hash,
                                       stack_context_t &context) noexcept {
    LOG_TRACE(log, "release block '{}'", hash);
    auto block = get_sync_info(folder_id).finish_fetching(hash, context);
    auto &block_proj = block_2_files.get<0>();
    auto &file_proj = block_2_files.get<1>();
    for (auto it = block_proj.find(block); it != block_proj.end();) {
        postponed_files.emplace(it->file);
        it = block_proj.erase(it);
    }
}

void controller_actor_t::cancel_sync(model::file_info_t *file) noexcept {
    if (block_iterator && block_iterator->get_source() == file) {
        block_iterator.reset();
    }
    auto id = file->get_full_id();
    auto &block_proj = block_2_files.get<0>();
    auto &file_proj = block_2_files.get<1>();
    for (auto it = file_proj.find(file); it != file_proj.end();) {
        // block_proj.erase(it->block);
        it = file_proj.erase(it);
    }
    if (auto it = synchronizing_files.find(id); it != synchronizing_files.end()) {
        synchronizing_files.erase(it);
    }
}

bool controller_actor_t::is_unflushed(model::file_info_t *peer_file, model::folder_info_t &peer_folder) noexcept {
    if (peer_file->is_file()) {
        auto peer_blocks_it = peer_file->iterate_blocks(0);
        if (peer_blocks_it.get_total() && peer_blocks_it.is_locally_available()) {
            return true;
        }
    }
    return false;
}
