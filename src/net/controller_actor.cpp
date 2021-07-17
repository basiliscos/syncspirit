#include "controller_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "../ui/messages.hpp"
#include <spdlog/spdlog.h>

using namespace syncspirit::net;

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
}
} // namespace

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster}, device{config.device}, peer{config.peer},
      peer_addr{config.peer_addr}, request_timeout{config.request_timeout}, peer_cluster_config{std::move(
                                                                                config.peer_cluster_config)},
      ignored_folders{config.ignored_folders}, sync_state{sync_state_t::none} {}

void controller_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        std::string id = "controller/";
        id += peer->device_id.get_short();
        p.set_identity(id, false);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::db, db, false).link(true);
        p.discover_name(names::fs, fs, false).link(true);
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(peer_addr, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
        p.subscribe_actor(&controller_actor_t::on_store_folder);
        p.subscribe_actor(&controller_actor_t::on_store_folder_info);
        p.subscribe_actor(&controller_actor_t::on_ready);
        p.subscribe_actor(&controller_actor_t::on_block);
        p.subscribe_actor(&controller_actor_t::on_write);
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("{}, on_start", identity);
    auto unknown_folders = cluster->update(*peer_cluster_config);
    for (auto &folder : unknown_folders) {
        if (!ignored_folders->by_key(folder.id())) {
            for (int i = 0; i < folder.devices_size(); ++i) {
                auto &d = folder.devices(i);
                if (d.id() == peer->get_id()) {
                    auto &dest = supervisor->get_address();
                    send<ui::payload::new_folder_notify_t>(dest, folder, peer, d.index_id());
                }
            }
        }
    }

    send<payload::start_reading_t>(peer_addr, get_address());
    ready();
    spdlog::info("{} is ready/online", identity);
}

void controller_actor_t::ready(model::file_info_ptr_t file) noexcept {
    if (!(substate & READY)) {
        send<payload::ready_signal_t>(get_address(), file);
        substate = substate | READY;
    }
}

void controller_actor_t::shutdown_start() noexcept {
    send<payload::termination_t>(peer_addr, shutdown_reason);
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::on_ready(message::ready_signal_t &message) noexcept {
    spdlog::trace("{}, on_ready", identity);
    substate = substate & ~READY;
    if (substate & BLOCK) {
        return;
    }

    model::file_info_ptr_t file = message.payload.file;
    if (!file) {
        file = cluster->file_for_synch(peer);
    }
    if (!file) {
        return;
    }
    spdlog::debug("{}, will try to update {}/{} ({} block(s) in total)", identity, file->get_folder()->label(),
                  file->get_name(), file->get_blocks().size());
    auto cluster_block = file->next_block();
    if (!cluster_block) {
        spdlog::trace("{}, no more blocks are needed for {}", identity, file->get_name());
        return ready();
    }

    auto existing_block = cluster_block.block->local_file();
    if (existing_block) {
        spdlog::trace("{}, cloning block {} from {} to {} as block {}", identity, existing_block.file_info->get_name(),
                      existing_block.block_index, file->get_name(), cluster_block.block_index);
        file->clone_block(*existing_block.file_info, existing_block.block_index, cluster_block.block_index);
        return ready(file);
    }
    request_block(file, cluster_block);
}

void controller_actor_t::request_block(const model::file_info_ptr_t &file,
                                       const model::block_location_t &block) noexcept {
    spdlog::trace("{} request_block, file = {}, block index = {}, sz = {}", identity, file->get_name(),
                  block.block_index, block.block->get_size());
    request<payload::block_request_t>(peer_addr, file, model::block_info_ptr_t{block.block}, block.block_index)
        .send(request_timeout);
    substate = substate | BLOCK;
}

bool controller_actor_t::on_unlink(const r::address_ptr_t &peer_addr) noexcept {
    auto it = peers_map.find(peer_addr);
    if (it != peers_map.end()) {
        auto &device = it->second;
        spdlog::debug("{}, on_unlink with {}", identity, device->device_id);
        peers_map.erase(it);
        if (peers_map.empty()) {
            sync_state = sync_state_t::none;
        }
        resources->release(resource::peer);
        return false;
    }
    return r::actor_base_t::on_unlink(peer_addr);
}

void controller_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    std::visit([this](auto &msg) { on_message(msg); }, message.payload);
}

void controller_actor_t::on_store_folder(message::store_folder_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &folder = message.payload.req->payload.request_payload.folder;
    auto &label = folder->label();
    if (ee) {
        spdlog::warn("{}, on_store_folder {} failed : {}", identity, label, ee->message());
        return do_shutdown(ee);
    }
    spdlog::trace("{}, on_store_folder_info {}", identity, label);
}

void controller_actor_t::on_store_folder_info(message::store_folder_info_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &fi = message.payload.req->payload.request_payload.folder_info;
    auto label = fi->get_folder()->label();
    spdlog::trace("{}, on_store_folder_info (max seq = {}) {}/{}", identity, fi->get_max_sequence(), label,
                  fi->get_db_key());
    if (ee) {
        spdlog::warn("{}, on_store_folder_info {} failed : {}", identity, label, ee->message());
        return do_shutdown(ee);
    }
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    auto &folder_id = message->folder();
    auto folder = cluster->get_folders().by_id(folder_id);
    if (!folder) {
        spdlog::warn("{}, unknown folder {}", identity, folder_id);
        auto ec = utils::make_error_code(utils::protocol_error_code_t::unknown_folder);
        std::string context = fmt::format("folder '{}'", folder_id);
        auto ee = r::make_error(context, ec);
        return do_shutdown(ee);
    }
    folder->update(*message, peer);
    auto updated = folder->is_dirty();
    spdlog::debug("{}, folder {}/{} has been updated = {}", identity, folder_id, folder->label(), updated);
    if (updated) {
        auto timeout = init_timeout / 2;
        request<payload::store_folder_request_t>(db, std::move(folder)).send(timeout);
    }
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept { std::abort(); }

void controller_actor_t::on_message(proto::message::Request &message) noexcept { std::abort(); }

void controller_actor_t::on_message(proto::message::DownloadProgress &message) noexcept { std::abort(); }

void controller_actor_t::on_block(message::block_response_t &message) noexcept {
    using request_t = fs::payload::write_request_t;
    substate = substate & ~BLOCK;
    auto ee = message.payload.ee;
    if (ee) {
        spdlog::warn("{}, can't receive block : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto &payload = message.payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &data = message.payload.res.data;
    bool final = file->get_blocks().size() == payload.block_index + 1;
    auto path = file->get_path();
    // request another block while the current is going to be flushed to disk
    auto request_id = request<request_t>(fs, path, std::move(data), final).send(init_timeout);
    file->mark_local_available(payload.block_index);
    ready(file);
    responses_map.emplace(request_id, &message);
}

void controller_actor_t::on_write(fs::message::write_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    if (ee) {
        spdlog::warn("{}, on_write failed : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto it = responses_map.find(message.payload.request_id());
    assert(it != responses_map.end());
    auto block_res = it->second;
    responses_map.erase(it);
    auto &p = block_res->payload.req->payload.request_payload;
    auto &file = p.file;
    if (file->get_status() == model::file_status_t::sync) {
        ready(nullptr);
        auto folder = file->get_folder();
        auto fi = folder->get_folder_info(device);
        auto seq = fi->get_max_sequence();
        auto new_seq = file->get_sequence();
        if (new_seq > seq) {
            spdlog::trace("{}, updated max sequence '{}' on local device: {} -> {}", identity, folder->label(), seq,
                          new_seq);
            fi->update_max_sequence(new_seq);
            request<payload::store_folder_info_request_t>(db, fi).send(init_timeout);
        }
    } else {
        ready(file);
    }
}
