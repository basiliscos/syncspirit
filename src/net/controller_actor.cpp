#include "controller_actor.h"
#include "names.h"
#include "../utils/error_code.h"
#include "../ui/messages.hpp"
#include <fstream>

using namespace syncspirit;
using namespace syncspirit::net;
namespace bfs = boost::filesystem;

template <typename Message> struct typed_folder_updater_t final : controller_actor_t::folder_updater_t {
    Message msg;

    typed_folder_updater_t(model::device_ptr_t &peer_, Message &&message_) {
        peer = peer_;
        msg = std::move(message_);
    }
    const std::string &id() noexcept override { return (*msg).folder(); }

    model::folder_info_ptr_t update(model::folder_t &folder) noexcept override {
        auto folder_info = folder.get_folder_info(peer);
        folder_info->update(*msg, peer);
        return folder_info;
    };
};

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
}
} // namespace

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster}, device{config.device}, peer{config.peer},
      peer_addr{config.peer_addr}, request_timeout{config.request_timeout}, peer_cluster_config{std::move(
                                                                                config.peer_cluster_config)},
      ignored_folders{config.ignored_folders}, request_pool{config.bep_config.rx_buff_size},
      blocks_max_kept{config.blocks_max_kept}, blocks_max_requested{config.blocks_max_requested} {
    log = utils::get_logger("net.controller_actor");
}

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
        p.discover_name(names::hasher_proxy, hasher_proxy, false).link();
    });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(peer_addr, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
        p.subscribe_actor(&controller_actor_t::on_store_folder_info);
        p.subscribe_actor(&controller_actor_t::on_new_folder);
        p.subscribe_actor(&controller_actor_t::on_ready);
        p.subscribe_actor(&controller_actor_t::on_block);
        p.subscribe_actor(&controller_actor_t::on_validation);
        p.subscribe_actor(&controller_actor_t::on_open);
        p.subscribe_actor(&controller_actor_t::on_close);
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
    send<payload::start_reading_t>(peer_addr, get_address());
    update(*peer_cluster_config);
    peer_cluster_config.reset();
    ready();
    LOG_INFO(log, "{} is ready/online", identity);
}

void controller_actor_t::update(proto::ClusterConfig &config) noexcept {
    LOG_TRACE(log, "{}, update", identity);
    auto unknown_folders = cluster->update(config);
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
    file_iterator.reset();
    block_iterator.reset();
}

void controller_actor_t::ready() noexcept { send<payload::ready_signal_t>(get_address()); }

void controller_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    send<payload::termination_t>(peer_addr, shutdown_reason);
    r::actor_base_t::shutdown_start();
}

controller_actor_t::ImmediateResult controller_actor_t::process_immediately() noexcept {
    assert(current_file);
    auto &path = current_file->get_path();
    auto parent = path.parent_path();
    sys::error_code ec;
    if (current_file->is_deleted()) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "{} removing {}", identity, path.string());
            auto ok = bfs::remove_all(path);
            if (!ok) {
                LOG_WARN(log, "{},  error removing {} : {}", identity, path.string(), ec.message());
                do_shutdown(make_error(ec));
                return ImmediateResult::ERROR;
            }
        }
        LOG_TRACE(log, "{}, {} already abscent, noop", identity, path.string());
        // current_file->record_update(*peer);
        return ImmediateResult::DONE;
    } else if (current_file->is_file() && current_file->get_size() == 0) {
        LOG_TRACE(log, "{}, creating empty file {}", identity, path.string());
        if (!bfs::exists(parent)) {
            bfs::create_directories(parent, ec);
            if (ec) {
                LOG_WARN(log, "{}, error creating path {} : {}", identity, parent.string(), ec.message());
                do_shutdown(make_error(ec));
                return ImmediateResult::ERROR;
            }
        }
        std::ofstream out;
        out.exceptions(out.failbit | out.badbit);
        try {
            out.open(path.string());
        } catch (const std::ios_base::failure &e) {
            do_shutdown(make_error(e.code()));
            LOG_WARN(log, "{}, error creating {} : {}", identity, path.string(), e.code().message());
            return ImmediateResult::ERROR;
        }
        // current_file->record_update(*peer);
        return ImmediateResult::DONE;
    } else if (current_file->is_dir()) {
        LOG_TRACE(log, "{}, creating dir {}", identity, path.string());
        if (!bfs::exists(path)) {
            bfs::create_directories(path, ec);
            if (ec) {
                LOG_WARN(log, "{}, error creating path {} : {}", identity, parent.string(), ec.message());
                do_shutdown(make_error(ec));
                return ImmediateResult::ERROR;
            }
        }
        // current_file->record_update(*peer);
        return ImmediateResult::DONE;
    } else if (current_file->is_link()) {
        auto target = bfs::path(current_file->get_link_target());
        LOG_TRACE(log, "{}, creating symlink {} -> {}", identity, path.string(), target.string());
        if (!bfs::exists(parent)) {
            bfs::create_directories(parent, ec);
            if (ec) {
                LOG_WARN(log, "{}, error creating parent path {} : {}", identity, parent.string(), ec.message());
                do_shutdown(make_error(ec));
                return ImmediateResult::ERROR;
            }
        }
        bfs::create_symlink(target, path, ec);
        if (ec) {
            LOG_WARN(log, "{}, error symlinking {} -> {} {} : {}", identity, path.string(), target.string(),
                     ec.message());
            do_shutdown(make_error(ec));
            return ImmediateResult::ERROR;
        }
        // current_file->record_update(*peer);
        return ImmediateResult::DONE;
    }
    return ImmediateResult::NON_IMMEDIATE;
}

void controller_actor_t::on_ready(message::ready_signal_t &message) noexcept {
    LOG_TRACE(log, "{}, on_ready, blocks requested = {}, kept = {}", identity, blocks_requested, blocks_kept);
    bool ignore = (blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                  || (blocks_kept > blocks_max_kept)                            // don't overload hasher / fs-writer
                  || (state != r::state_t::OPERATIONAL)                         // we are shutting down
                  || (!file_iterator && !block_iterator && blocks_requested)    // done
        ;

    if (ignore) {
        return;
    }

    if (!file_iterator && !block_iterator) {
        file_iterator = cluster->iterate_files(peer);
        if (!file_iterator) {
            LOG_TRACE(log, "{}, nothing more to sync", identity);
            return;
        }
    }

    if (block_iterator) {
        assert(current_file);
        auto cluster_block = block_iterator.next();
        auto existing_block = cluster_block.block->local_file();
        if (existing_block) {
            LOG_TRACE(log, "{}, cloning block {} from {} to {} as block {}", identity,
                      existing_block.file_info->get_name(), existing_block.block_index, current_file->get_name(),
                      cluster_block.block_index);
            current_file->clone_block(*existing_block.file_info, existing_block.block_index, cluster_block.block_index);
            ready();
        } else {
            request_block(cluster_block);
            ready();
        }
        if (!block_iterator) {
            LOG_TRACE(log, "{}, there are no more blocks for {}", identity, current_file->get_full_name());
            current_file.reset();
        }
        return;
    }

    current_file = file_iterator.next();
    auto ir = process_immediately();
    if (ir == ImmediateResult::ERROR) {
        return;
    }
    if (ir == ImmediateResult::NON_IMMEDIATE) {
        LOG_TRACE(log, "{}, going to sync {}", identity, current_file->get_full_name());
        block_iterator = current_file->iterate_blocks();
    }
    ready();
}

void controller_actor_t::request_block(const model::block_location_t &block) noexcept {
    auto sz = block.block->get_size();
    LOG_TRACE(log, "{} request_block, file = {}, block index = {}, sz = {}, request pool sz = {}, blocks_kept = {}",
              identity, current_file->get_full_name(), block.block_index, sz, request_pool, blocks_kept);
    request<payload::block_request_t>(peer_addr, current_file, model::block_info_ptr_t{block.block}, block.block_index)
        .send(request_timeout);
    ++blocks_requested;
    request_pool -= (int64_t)sz;
}

bool controller_actor_t::on_unlink(const r::address_ptr_t &peer_addr) noexcept {
    auto it = peers_map.find(peer_addr);
    if (it != peers_map.end()) {
        auto &device = it->second;
        LOG_DEBUG(log, "{}, on_unlink with {}", identity, device->device_id);
        peers_map.erase(it);
        resources->release(resource::peer);
        return false;
    }
    return r::actor_base_t::on_unlink(peer_addr);
}

void controller_actor_t::on_forward(message::forwarded_message_t &message) noexcept {
    std::visit([this](auto &msg) { on_message(msg); }, message.payload);
}

void controller_actor_t::on_new_folder(message::store_new_folder_notify_t &message) noexcept {
    auto &folder = message.payload.folder;
    LOG_TRACE(log, "{}, on_new_folder, folder = '{}'", identity, folder->label());
    auto cluster_update = cluster->get(peer);
    using payload_t = std::decay_t<decltype(cluster_update)>;
    auto update = std::make_unique<payload_t>(std::move(cluster_update));
    send<payload::cluster_config_t>(peer_addr, std::move(update));
}

void controller_actor_t::on_store_folder_info(message::store_folder_info_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &fi = message.payload.req->payload.request_payload.folder_info;
    auto label = fi->get_folder()->label();
    log->trace("{}, on_store_folder_info (max seq = {}) {}/{}", identity, fi->get_max_sequence(), label,
               fi->get_db_key());
    if (ee) {
        log->warn("{}, on_store_folder_info {} failed : {}", identity, label, ee->message());
        return do_shutdown(ee);
    }
    ready();
}

void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept { update(*message); }

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    update(typed_folder_updater_t(peer, std::move(message)));
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    update(typed_folder_updater_t(peer, std::move(message)));
}

void controller_actor_t::on_message(proto::message::Request &message) noexcept { std::abort(); }

void controller_actor_t::on_message(proto::message::DownloadProgress &message) noexcept { std::abort(); }

void controller_actor_t::update(folder_updater_t &&updater) noexcept {
    auto &folder_id = updater.id();
    auto folder = cluster->get_folders().by_id(folder_id);
    if (current_file && current_file->get_folder_info()->get_folder()->id() == folder_id) {
        LOG_TRACE(log, "{}, resetting iterators on folder {}", identity, folder->label());
        file_iterator.reset();
        block_iterator.reset();
    }
    if (!folder) {
        LOG_WARN(log, "{}, unknown folder {}", identity, folder_id);
        auto ec = utils::make_error_code(utils::protocol_error_code_t::unknown_folder);
        std::string context = fmt::format("folder '{}'", folder_id);
        auto ee = r::make_error(context, ec);
        return do_shutdown(ee);
    }
    auto folder_info = updater.update(*folder);
    auto updated = folder_info->is_dirty();
    LOG_DEBUG(log, "{}, folder info {} for folder '{}' has been updated = {}", identity, folder_info->get_index(),
              folder->label(), updated);
    if (updated) {
        auto timeout = init_timeout / 2;
        request<payload::store_folder_info_request_t>(db, std::move(folder_info)).send(timeout);
    }
}

void controller_actor_t::on_block(message::block_response_t &message) noexcept {
    --blocks_requested;
    auto ee = message.payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, can't receive block : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    ++blocks_kept;

    auto &payload = message.payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &data = message.payload.res.data;
    auto block_index = payload.block_index;
    auto &blocks = file->get_blocks();
    bool final = file->mark_local_available(block_index);
    auto &path = file->get_path();
    auto &block = *payload.block;
    auto &hash = block.get_hash();
    request_pool += block.get_size();

    auto &path_str = path.string();
    if (write_map.count(path_str) == 0) {
        using request_t = fs::payload::open_request_t;
        write_map[path_str].file = file;
        auto file_sz = static_cast<size_t>(file->get_size());
        request<request_t>(fs, path, file_sz).send(init_timeout);
    }

    auto &info = write_map.at(path_str);
    info.final = final;
    ++info.pending_blocks;
    intrusive_ptr_add_ref(&message);
    request<hasher::payload::validation_request_t>(hasher_proxy, data, std::move(hash), &message).send(init_timeout);
    return ready();
}

void controller_actor_t::on_open(fs::message::open_response_t &res) noexcept {
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_open failed : {}", identity, ee->message());
        return do_shutdown(ee);
    }

    auto &path_str = payload.req->payload.request_payload.path.string();
    auto it = write_map.find(path_str);
    assert(it != write_map.end());
    auto &info = it->second;
    info.file_desc = std::move(payload.res->file);
    if (info.validated_blocks.size()) {
        write_blocks(it);
    }
}

void controller_actor_t::on_close(fs::message::close_response_t &res) noexcept {
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_close failed : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto &path_str = payload.req->payload.request_payload->path.string();
    LOG_INFO(log, "{}, file '{}' has been flushed to disk", identity, path_str);
    auto it = write_map.find(path_str);
    assert(it != write_map.end());
    auto &info = it->second;

    write_map.erase(it);
}

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    auto &ee = res.payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_validation failed : {}", identity, ee->message());
        return do_shutdown(ee);
    }
    auto block_res = (message::block_response_t *)res.payload.req->payload.request_payload->custom;
    auto &payload = block_res->payload.req->payload.request_payload;
    auto &file = payload.file;
    auto &path = file->get_path();

    if (!res.payload.res.valid) {
        std::string context = fmt::format("digest mismatch for {}", path.string());
        auto ec = utils::make_error_code(utils::protocol_error_code_t::digest_mismatch);
        LOG_WARN(log, "{}, check_digest, digest mismatch: {}", identity, context);
        auto ee = r::make_error(context, ec);
        do_shutdown(ee);
    } else {
        auto it = write_map.find(path.string());
        assert(it != write_map.end());
        auto &info = it->second;
        --info.pending_blocks;
        info.validated_blocks.emplace_back(block_res);
        if (info.file_desc) {
            write_blocks(it);
        }
    }
    intrusive_ptr_release(block_res);
}

void controller_actor_t::write_blocks(write_it_t it) noexcept {
    auto &info = it->second;
    auto disk_view = info.file_desc->data();
    auto &blocks = info.validated_blocks;
    while (!blocks.empty()) {
        auto &res = blocks.front();
        auto &payload = res->payload;
        auto &req_payload = payload.req->payload.request_payload;
        auto &file = req_payload.file;
        auto &data = payload.res.data;
        auto block_index = req_payload.block_index;
        auto offset = file->get_block_offset(block_index);
        std::copy(data.begin(), data.end(), disk_view + offset);
        blocks.pop_front();
        --blocks_kept;
    }
    if (info.final && info.pending_blocks == 0) {
        request<fs::payload::close_request_t>(fs, std::move(info.file_desc), info.file->get_path()).send(init_timeout);
    }
    ready();
}
