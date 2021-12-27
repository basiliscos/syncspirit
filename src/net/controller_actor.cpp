#include "controller_actor.h"
#include "names.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/new_file.h"
#include "../utils/error_code.h"
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
    : r::actor_base_t{config}, cluster{config.cluster}, peer{config.peer},
      peer_addr{config.peer_addr}, request_timeout{config.request_timeout},
      request_pool{config.request_pool},
      blocks_max_requested{config.blocks_max_requested} {
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
#if 0
    update(*peer_cluster_config);
    peer_cluster_config.reset();
#endif
    ready();
    LOG_INFO(log, "{} is ready/online", identity);
}

void controller_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    send<payload::termination_t>(peer_addr, shutdown_reason);
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    r::actor_base_t::shutdown_finish();
}

void controller_actor_t::on_termination(message::termination_signal_t& message) noexcept {
    resources->release(resource::peer);
    if (resources->has(resource::peer)) {
        auto& ee = message.payload.ee;
        LOG_TRACE(log, "{}, on_termination reason: {}", identity, ee->message());
        do_shutdown(ee);
    }
}

void controller_actor_t::ready() noexcept {
    send<payload::ready_signal_t>(get_address());
}


void controller_actor_t::on_ready(message::ready_signal_t &message) noexcept {
    LOG_TRACE(log, "{}, on_ready, blocks requested = {}", identity, blocks_requested);
    bool ignore = (blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                  || (state != r::state_t::OPERATIONAL)                         // wrequest pool sz = 32505856e are shutting down
            ;
                  //|| (!file_iterator && !block_iterator && blocks_requested)    // done

    if (ignore) {
        return;
    }

    if (file && file->get_size()) {
        auto block = cluster->next_block(file, !(substate & substate_t::iterating_blocks));
        if (block) {
            substate |= substate_t::iterating_blocks;
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
            LOG_TRACE(log, "{}, next_file = {}", identity, file->get_name());
            using blocks_t = std::vector<proto::BlockInfo>;
            auto diff = model::diff::cluster_diff_ptr_t{};
            auto info = file->as_proto(false);
            auto& source_blocks = file->get_blocks();
            auto blocks = blocks_t();
            blocks.reserve(source_blocks.size());
            for(auto& b: source_blocks) {
                proto::BlockInfo bi;
                bi.set_hash(std::string(b->get_hash()));
                blocks.emplace_back(std::move(bi));
            }
            diff = new model::diff::modify::new_file_t(
                *cluster,
                file->get_folder_info()->get_folder()->get_id(),
                std::move(info),
                std::move(blocks)
            );
            cluster->next_block(file, true);
            send<payload::model_update_t>(coordinator, std::move(diff), this);
        } else {
            substate = substate & ~substate_t::iterating_files;
        }
    }
}

void controller_actor_t::preprocess_block(model::file_block_t &file_block) noexcept {
    using namespace model::diff;
    auto folder = file->get_folder_info()->get_folder();
    auto folder_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto target_file = folder_info->get_file_infos().by_name(file->get_name());
    assert(target_file);

    if (file_block.is_locally_available()) {
        auto diff = block_diff_ptr_t(new modify::clone_block_t(*target_file, *file_block.block()));
        send<payload::block_update_t>(coordinator, std::move(diff), this);
    }
    else {
        auto block = file_block.block();
        block->lock();
        auto sz = block->get_size();
        LOG_TRACE(log, "{} request_block, file = {}, block index = {}, sz = {}, request pool sz = {}",
                  identity, file->get_full_name(), file_block.block_index(), sz, request_pool);
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

void controller_actor_t::on_model_update(message::model_update_t &message) noexcept {
    if (message.payload.custom == this) {
        LOG_TRACE(log, "{}, on_model_update", identity);
        auto& diff = *message.payload.diff;
        auto r = diff.visit(*this);
        if (!r) {
            auto ee = make_error(r.assume_error());
            do_shutdown(ee);
        }
    }
}


void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "{}, error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Request &message) noexcept { std::abort(); }

void controller_actor_t::on_message(proto::message::DownloadProgress &message) noexcept { std::abort(); }


auto controller_actor_t::operator()(const model::diff::modify::new_file_t &) noexcept -> outcome::result<void>  {
    ready();
    return outcome::success();
}

auto controller_actor_t::operator()(const model::diff::peer::update_folder_t &) noexcept -> outcome::result<void> {
    ready();
    return outcome::success();
}



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

    intrusive_ptr_add_ref(&message);
    auto &data = message.payload.res.data;
    request<hasher::payload::validation_request_t>(hasher_proxy, data, hash, &message).send(init_timeout);
    resources->acquire(resource::hash);
    return ready();
}

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    using namespace model::diff;
    resources->release(resource::hash);
    auto &ee = res.payload.ee;
    auto block_res = (message::block_response_t *)res.payload.req->payload.request_payload->custom;
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
        }
        else {
            auto &data = block_res->payload.res.data;
            auto index = payload.block.block_index();

            auto diff = block_diff_ptr_t(new modify::append_block_t(*file, index, std::move(data)));
            send<payload::block_update_t>(coordinator, std::move(diff), this);
        }
    }
    block->unlock();
    intrusive_ptr_release(block_res);
}
