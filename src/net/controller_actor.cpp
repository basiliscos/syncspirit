#include "controller_actor.h"
#include "names.h"
#include "model/diff/aggregate.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/modify/finish_file.h"
#include "model/diff/modify/flush_file.h"
#include "utils/error_code.h"
#include <fstream>

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
      request_timeout{config.request_timeout}, request_pool{config.request_pool}, blocks_max_requested{
                                                                                      config.blocks_max_requested} {
    log = utils::get_logger("net.controller_actor");
}

void controller_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "controller/";
        id += peer->device_id().get_short();
        p.set_identity(id, false);
        open_reading = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
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
        p.subscribe_actor(&controller_actor_t::on_ready);
        p.subscribe_actor(&controller_actor_t::on_termination);
        p.subscribe_actor(&controller_actor_t::on_block);
        p.subscribe_actor(&controller_actor_t::on_validation);
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
    send<payload::start_reading_t>(peer_addr, get_address(), true);

    auto cluster_config = cluster->generate(*peer);
    using payload_t = std::decay_t<decltype(cluster_config)>;
    auto payload = std::make_unique<payload_t>(std::move(cluster_config));
    send<payload::forwarded_message_t>(peer_addr, std::move(payload));

    resources->acquire(resource::peer);
    resources->acquire(resource::peer);
    LOG_INFO(log, "{} is online", identity);
}

void controller_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    send<payload::termination_t>(peer_addr, shutdown_reason);
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish, blocks_requested = {}", identity, blocks_requested);
    r::actor_base_t::shutdown_finish();
}

void controller_actor_t::on_termination(message::termination_signal_t &message) noexcept {
    resources->release(resource::peer);
    if (resources->has(resource::peer)) {
        auto &ee = message.payload.ee;
        LOG_TRACE(log, "{}, on_termination reason: {}", identity, ee->message());
        do_shutdown(ee);
    }
}

void controller_actor_t::ready() noexcept { send<payload::ready_signal_t>(get_address()); }

void controller_actor_t::on_ready(message::ready_signal_t &message) noexcept {
    LOG_TRACE(log, "{}, on_ready, blocks requested = {}", identity, blocks_requested);
    bool ignore = (blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                  || (state != r::state_t::OPERATIONAL) // wrequest pool sz = 32505856e are shutting down
        ;
    //|| (!file_iterator && !block_iterator && blocks_requested)    // done

    if (ignore) {
        return;
    }

    if (file && file->get_size() && file->local_file()) {
        auto reset_block = !(substate & substate_t::iterating_blocks);
        auto block = cluster->next_block(file, reset_block);
        if (block) {
            substate |= substate_t::iterating_blocks;
            if (reset_block) {
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::lock_file_t(*file, true);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            }
            preprocess_block(block);
        } else {
            substate = substate & ~substate_t::iterating_blocks;
        }
    }
    if (!(substate & substate_t::iterating_blocks)) {
        bool reset_file = !(substate & substate_t::iterating_files) && !blocks_requested;
        file = cluster->next_file(peer, reset_file);
        if (file) {
            substate |= substate_t::iterating_files;
            if (!file->local_file()) {
                LOG_DEBUG(log, "{}, next_file = {}", identity, file->get_name());
                file->locally_lock();
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::clone_file_t(*file);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            } else {
                ready();
            }
        } else {
            substate = substate & ~substate_t::iterating_files;
        }
    }
}

void controller_actor_t::preprocess_block(model::file_block_t &file_block) noexcept {
    using namespace model::diff;
    auto target_file = file->local_file();
    assert(target_file);

    if (file_block.is_locally_available()) {
        auto diff = block_diff_ptr_t(new modify::clone_block_t(*target_file, *file_block.block()));
        send<model::payload::block_update_t>(coordinator, std::move(diff), this);
    } else {
        auto block = file_block.block();
        block->lock();
        auto sz = block->get_size();
        LOG_TRACE(log, "{} request_block, file = {}, block index = {}, sz = {}, request pool sz = {}", identity,
                  file->get_full_name(), file_block.block_index(), sz, request_pool);
        request<payload::block_request_t>(peer_addr, file, file_block).send(request_timeout);
        ++blocks_requested;
        request_pool -= (int64_t)sz;
    }
    ready();
}

void controller_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    if (state != r::state_t::OPERATIONAL) {
        return;
    }
    std::visit([this](auto &msg) { on_message(msg); }, message.payload);
}

void controller_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    if (message.payload.custom == this) {
        LOG_TRACE(log, "{}, on_model_update", identity);
        auto &diff = *message.payload.diff;
        auto r = diff.visit(*this);
        if (!r) {
            auto ee = make_error(r.assume_error());
            return do_shutdown(ee);
        }
        ready();
    }
}

auto controller_actor_t::operator()(const model::diff::modify::clone_file_t &diff) noexcept -> outcome::result<void> {
    auto folder_id = diff.folder_id;
    auto file_name = diff.file.name();
    auto folder = cluster->get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(peer);
    auto file = folder_info->get_file_infos().by_name(file_name);
    file->locally_unlock();
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::modify::finish_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device(peer);
    auto file = folder_info->get_file_infos().by_name(diff.file_name);
    assert(file);
    auto update = model::diff::cluster_diff_ptr_t{};
    update = new model::diff::modify::flush_file_t(*file);
    send<model::payload::model_update_t>(coordinator, std::move(update), this);
    return outcome::success();
}

void controller_actor_t::on_block_update(model::message::block_update_t &message) noexcept {
    if (message.payload.custom == this) {
        LOG_TRACE(log, "{}, on_block_update", identity);
        auto &d = *message.payload.diff;
        auto folder = cluster->get_folders().by_id(d.folder_id);
        auto folder_info = folder->get_folder_infos().by_device_id(d.device_id);
        auto source_file = folder_info->get_file_infos().by_name(d.file_name);
        if (source_file->is_locally_available()) {
            using diffs_t = model::diff::aggregate_t::diffs_t;
            LOG_TRACE(log, "{}, on_block_update, finalizing", identity);
            auto my_file = source_file->local_file();
            auto diffs = diffs_t{};
            diffs.push_back(new model::diff::modify::lock_file_t(*my_file, false));
            diffs.push_back(new model::diff::modify::finish_file_t(*my_file));
            auto diff = model::diff::cluster_diff_ptr_t{};
            diff = new model::diff::aggregate_t(std::move(diffs));
            send<model::payload::model_update_t>(coordinator, std::move(diff), this);
        }
    }
}

void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept {
    LOG_DEBUG(log, "{}, on_message (ClusterConfig)", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    LOG_DEBUG(log, "{}, on_message (Index)", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    LOG_DEBUG(log, "{}, on_message (IndexUpdate)", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto &ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<model::payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Request &message) noexcept { std::abort(); }

void controller_actor_t::on_message(proto::message::DownloadProgress &message) noexcept { std::abort(); }

void controller_actor_t::on_block(message::block_response_t &message) noexcept {
    --blocks_requested;
    auto ee = message.payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, can't receive block : {}", identity, ee->message());
        return do_shutdown(ee);
    }

    auto &payload = message.payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &file_block = payload.block;
    auto &block = *file_block.block();
    auto hash = std::string(file_block.block()->get_hash());
    request_pool += block.get_size();

    auto &data = message.payload.res.data;
    request<hasher::payload::validation_request_t>(hasher_proxy, data, hash, &message).send(init_timeout);
    resources->acquire(resource::hash);
    return ready();
}

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    using namespace model::diff;
    resources->release(resource::hash);
    auto &ee = res.payload.ee;
    auto block_res = (message::block_response_t *)res.payload.req->payload.request_payload->custom.get();
    auto &payload = block_res->payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &path = file->get_path();
    auto block = payload.block.block();

    if (ee) {
        LOG_WARN(log, "{}, on_validation failed : {}", identity, ee->message());
        do_shutdown(ee);
    } else {
        if (!res.payload.res.valid) {
            std::string context = fmt::format("digest mismatch for {}", path.string());
            auto ec = utils::make_error_code(utils::protocol_error_code_t::digest_mismatch);
            LOG_WARN(log, "{}, check_digest, digest mismatch: {}", identity, context);
            auto ee = r::make_error(context, ec);
            do_shutdown(ee);
        } else {
            auto &data = block_res->payload.res.data;
            auto index = payload.block.block_index();

            auto diff = block_diff_ptr_t(new modify::append_block_t(*file, index, std::move(data)));
            send<model::payload::block_update_t>(coordinator, std::move(diff), this);
        }
    }
    block->unlock();
}
