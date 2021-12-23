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

#if 0
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
#endif

namespace {
namespace resource {
r::plugin::resource_id_t peer = 0;
//r::plugin::resource_id_t file = 1;
} // namespace resource
} // namespace

#if 0
controller_actor_t::write_info_t::write_info_t(const model::file_info_ptr_t &file_) noexcept
    : file{file_}, pending_blocks{0}, opening{false} {
    auto &blocks = file->get_blocks();
    blocks_left = blocks.size();
    for (auto &b : blocks) {
        if (b) {
            blocks_left--;
        }
    }
}

bool controller_actor_t::write_info_t::done() const noexcept {
    return pending_blocks == 0 && clone_queue.empty() && validated_blocks.empty();
}

bool controller_actor_t::write_info_t::complete() const noexcept { return blocks_left == 0 && done(); }
#endif

controller_actor_t::controller_actor_t(config_t &config)
    : r::actor_base_t{config}, cluster{config.cluster}, peer{config.peer},
      peer_addr{config.peer_addr}, request_timeout{config.request_timeout},
      request_pool{config.bep_config.rx_buff_size},
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
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) {
        p.link(peer_addr, false);
        //p.on_unlink([this](auto &message) { return this->on_unlink(message); });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&controller_actor_t::on_forward);
        p.subscribe_actor(&controller_actor_t::on_ready);
        p.subscribe_actor(&controller_actor_t::on_termination);
        p.subscribe_actor(&controller_actor_t::on_block);
        p.subscribe_actor(&controller_actor_t::on_validation);
#if 0
        p.subscribe_actor(&controller_actor_t::on_store_folder_info);
        p.subscribe_actor(&controller_actor_t::on_store_file_info);
        p.subscribe_actor(&controller_actor_t::on_new_folder);
        p.subscribe_actor(&controller_actor_t::on_file_update);
        p.subscribe_actor(&controller_actor_t::on_open);
        p.subscribe_actor(&controller_actor_t::on_close);
        p.subscribe_actor(&controller_actor_t::on_clone);
#endif
    });
}

void controller_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
    send<payload::start_reading_t>(peer_addr, get_address(), true);

    auto cluster_config = cluster->generate(*peer);
    using payload_t = std::decay_t<decltype(cluster_config)>;
    auto payload = std::make_unique<payload_t>(std::move(cluster_config));
    send<payload::cluster_config_t>(peer_addr, std::move(payload));

#if 0
    update(*peer_cluster_config);
    peer_cluster_config.reset();
#endif
    ready();
    LOG_INFO(log, "{} is ready/online", identity);
}

#if 0
void controller_actor_t::update(proto::ClusterConfig &config) noexcept {
    LOG_TRACE(log, "{}, update", identity);
    auto update_result = cluster->update(config);
    for (auto folder : update_result.unknown_folders) {
        if (!ignored_folders->by_key(folder->id())) {
            for (int i = 0; i < folder->devices_size(); ++i) {
                auto &d = folder->devices(i);
                if (d.id() == peer->get_id()) {
                    auto &dest = supervisor->get_address();
                    send<ui::payload::new_folder_notify_t>(dest, *folder, peer, d.index_id());
                }
            }
        }
    }
    auto &folders = cluster->get_folders();
    for (auto f : update_result.outdated_folders) {
        auto folder = folders.by_id(f->id());
        LOG_DEBUG(log, "sending index of {} ", folder->label());
        send<payload::folder_update_t>(peer_addr, std::move(folder));
    }
    file_iterator.reset();
    block_iterator.reset();
}
#endif

void controller_actor_t::shutdown_start() noexcept {
    LOG_TRACE(log, "{}, shutdown_start", identity);
    send<payload::termination_t>(peer_addr, shutdown_reason);
#if 0
    for (auto &it : write_map) {
        auto &info = it.second;
        auto &sink = info.sink;
        auto &path = info.file->get_path();
        if (sink) {
            request<fs::payload::close_request_t>(file_addr, std::move(sink), path, info.complete()).send(init_timeout);
        }
    }
#endif
    r::actor_base_t::shutdown_start();
}

void controller_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
#if 0
    assert(write_map.empty());
#endif
    r::actor_base_t::shutdown_finish();
}

#if 0
bool controller_actor_t::on_unlink(unlink_request_t &message) noexcept {
    LOG_TRACE(log, "{}, on_unlink");
    do_shutdown();
    return true;
#if 0
    auto &source = message.payload.request_payload.server_addr;
    if (source == peer_addr) {

        return false;
    }
    unlink_requests.emplace_back(&message);
#endif
    return true;
}
#endif


void controller_actor_t::on_termination(message::termination_signal_t& message) noexcept {
    auto& ee = message.payload.ee;
    LOG_TRACE(log, "{}, on_termination reason: {}", identity, ee->message());
    do_shutdown(ee);
}

void controller_actor_t::ready() noexcept {
    send<payload::ready_signal_t>(get_address());
}

#if 0
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
        } else {
            LOG_TRACE(log, "{}, {} already abscent, noop", identity, path.string());
        }
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

        if (!bfs::exists(path)) {
            std::ofstream out;
            out.exceptions(out.failbit | out.badbit);
            try {
                out.open(path.string());
            } catch (const std::ios_base::failure &e) {
                do_shutdown(make_error(e.code()));
                LOG_WARN(log, "{}, error creating {} : {}", identity, path.string(), e.code().message());
                return ImmediateResult::ERROR;
            }
        }
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

        bool attempt_create =
            !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
        if (attempt_create) {
            bfs::create_symlink(target, path, ec);
            if (ec) {
                LOG_WARN(log, "{}, error symlinking {} -> {} {} : {}", identity, path.string(), target.string(),
                         ec.message());
                do_shutdown(make_error(ec));
                return ImmediateResult::ERROR;
            }
        } else {
            LOG_TRACE(log, "{}, no need to create symlink {} -> {}", identity, path.string(), target.string());
        }
        return ImmediateResult::DONE;
    }
    return ImmediateResult::NON_IMMEDIATE;
}
#endif

void controller_actor_t::on_ready(message::ready_signal_t &message) noexcept {
    LOG_TRACE(log, "{}, on_ready, blocks requested = {}", identity, blocks_requested);
    bool ignore = (blocks_requested > blocks_max_requested || request_pool < 0) // rx buff is going to be full
                  || (state != r::state_t::OPERATIONAL)                         // wrequest pool sz = 32505856e are shutting down
            ;
                  //|| (!file_iterator && !block_iterator && blocks_requested)    // done
        ;

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
#if 0

    if (!file_iterator && !block_iterator) {
        file_iterator = cluster->iterate_files(peer);
        if (!file_iterator) {
            LOG_DEBUG(log, "{}, nothing more to sync", identity);
            return;
        }
    }

    if (block_iterator) {
        assert(current_file);
        auto cluster_block = block_iterator.next();
        auto existing_block = cluster_block.block()->local_file();
        if (existing_block) {
            clone_block(cluster_block, existing_block);
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

    auto source_file = file_iterator.next();
    current_file = source_file->link(device);

    auto ir = process_immediately();
    if (ir == ImmediateResult::ERROR) {
        return;
    } else if (ir == ImmediateResult::NON_IMMEDIATE) {
        LOG_TRACE(log, "{}, going to sync {}", identity, current_file->get_full_name());
        block_iterator = model::blocks_interator_t(*source_file, *current_file);
    } else if (ir == ImmediateResult::DONE) {
        current_file->after_sync();
        request<payload::store_file_request_t>(db, current_file, nullptr).send(init_timeout);
    }
    ready();
#endif
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


#if 0
void controller_actor_t::clone_block(const model::file_block_t &block, model::file_block_t &local) noexcept {
    LOG_TRACE(log, "{}, cloning block {} from {} to {} as block {}", identity, local.block_index(),
              local.file()->get_name(), current_file->get_name(), block.block_index());
    auto &path = current_file->get_path();
    auto &path_str = path.string();

    if (write_map.count(path_str) == 0) {
        using request_t = fs::payload::clone_request_t;
        write_map.emplace(path_str, current_file);
        auto target_sz = static_cast<size_t>(current_file->get_size());
        auto block_sz = static_cast<size_t>(block.block()->get_size());
        auto &source = local.file()->get_path();
        auto source_offset = local.get_offset();
        auto target_offset = block.get_offset();
        resources->acquire(resource::file);
        request<request_t>(file_addr, source, path, target_sz, block_sz, source_offset, target_offset)
            .send(init_timeout);
    }
    auto &info = write_map.at(path_str);
    auto cloned_block =
        clone_block_t{block.block(), model::file_info_ptr_t{local.file()}, local.block_index(), block.block_index()};
    info.clone_queue.emplace_back(std::move(cloned_block));
    ready();
}

void controller_actor_t::request_block(const model::file_block_t &fb) noexcept {
    auto sz = fb.block()->get_size();
    LOG_TRACE(log, "{} request_block, file = {}, block index = {}, sz = {}, request pool sz = {}, blocks_kept = {}",
              identity, current_file->get_full_name(), fb.block_index(), sz, request_pool, blocks_kept);
    request<payload::block_request_t>(peer_addr, current_file, fb).send(request_timeout);
    ++blocks_requested;
    request_pool -= (int64_t)sz;
}
#endif

bool controller_actor_t::on_unlink(const r::address_ptr_t &peer_addr) noexcept {
    auto it = peers_map.find(peer_addr);
    if (it != peers_map.end()) {
        auto &device = it->second;
        LOG_DEBUG(log, "{}, on_unlink with {}", identity, device->device_id());
        peers_map.erase(it);
        resources->release(resource::peer);
        return false;
    }
    return r::actor_base_t::on_unlink(peer_addr);
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

#if 0
void controller_actor_t::on_new_folder(message::store_new_folder_notify_t &message) noexcept {
    auto &folder = message.payload.folder;
    LOG_TRACE(log, "{}, on_new_folder, folder = '{}'", identity, folder->get_label());
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
        do_shutdown(ee);
    }
    if (state != r::state_t::OPERATIONAL) {
        if (write_map.empty() && !unlink_requests.empty()) {
            using plugin_t = r::plugin::link_client_plugin_t;
            auto plugin = static_cast<plugin_t *>(get_plugin(plugin_t::class_identity));
            for (auto &it : unlink_requests) {
                plugin->forget_link(*it);
            }
            unlink_requests.resize(0);
        }
    } else {
        ready();
    }
}

void controller_actor_t::on_store_file_info(message::store_file_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &payload = message.payload.req->payload.request_payload;
    auto &file = payload.file;
    auto release = payload.custom;
    if (release) {
        resources->release(resource::file);
    }
    log->trace("{}, on_store_file_info, file: {}, seq: {}", identity, file->get_full_name(), file->get_sequence());
    if (ee) {
        log->error("{}, on_store_file_info, file: {}, error: {}", identity, file->get_full_name(), ee->message());
        return do_shutdown(ee);
    }
    auto &dest = supervisor->get_address();
    send<payload::file_update_t>(dest, file);
    // ready();
}
#endif

void controller_actor_t::on_message(proto::message::ClusterConfig &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::Index &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", identity, peer->device_id(), ec.message());
        return do_shutdown(make_error(ec));
    }
    send<payload::model_update_t>(coordinator, std::move(diff_opt.assume_value()), this);
}

void controller_actor_t::on_message(proto::message::IndexUpdate &message) noexcept {
    LOG_DEBUG(log, "{}, on_message", identity);
    auto diff_opt = cluster->process(*message, *peer);
    if (!diff_opt) {
        auto& ec = diff_opt.assume_error();
        LOG_ERROR(log, "error processing message from {} : {}", identity, peer->device_id(), ec.message());
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



#if 0
void controller_actor_t::update(folder_updater_t &&updater) noexcept {
    auto &folder_id = updater.id();
    auto folder = cluster->get_folders().by_id(folder_id);
    if (current_file && current_file->get_folder_info()->get_folder()->get_id() == folder_id) {
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
#endif

#if 0
controller_actor_t::write_info_t &controller_actor_t::record_block_data(model::file_info_ptr_t &file,
                                                                        std::size_t block_index) noexcept {
    using request_t = fs::payload::open_request_t;
    auto &path = file->get_path();
    auto &path_str = path.string();
    if (write_map.count(path_str) == 0) {
        write_map.emplace(path_str, file);
    }
    auto &info = write_map.at(path_str);
    --info.blocks_left;
    if (!info.sink && info.clone_queue.empty() && !info.opening) {
        auto file_sz = static_cast<size_t>(file->get_size());
        request<request_t>(file_addr, path, file_sz).send(init_timeout);
        info.opening = true;
    }
    return info;
}
#endif

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
    return ready();
}

#if 0
void controller_actor_t::on_open(fs::message::open_response_t &res) noexcept {
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_open failed : {}", identity, ee->message());
        resources->release(resource::file);
        return do_shutdown(ee);
    }

    resources->acquire(resource::file);
    auto &path_str = payload.req->payload.request_payload.path.string();
    LOG_TRACE(log, "{}, on_open, {}", identity, path_str);
    auto it = write_map.find(path_str);
    assert(it != write_map.end());
    auto &info = it->second;
    info.sink = std::move(payload.res->file);
    assert(info.sink);
    if (info.validated_blocks.size()) {
        process(it);
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

    auto &file = it->second.file;
    file->after_sync();
    auto release_resouce = this;
    request<payload::store_file_request_t>(db, file, release_resouce).send(init_timeout);

    write_map.erase(it);
}

void controller_actor_t::on_clone(fs::message::clone_response_t &res) noexcept {
    auto &payload = res.payload;
    auto &ee = payload.ee;
    if (ee) {
        LOG_WARN(log, "{}, on_clone failed : {}", identity, ee->message());
        return do_shutdown(ee);
    }

    auto &opened_file = payload.res->file;
    auto &path_str = payload.req->payload.request_payload.target.string();
    LOG_TRACE(log, "{}, cloned block for '{}' ", identity, path_str);
    auto it = write_map.find(path_str);
    assert(it != write_map.end());
    auto &info = it->second;
    auto block_info = info.clone_queue.front();
    info.file->assign_block(block_info.block, block_info.target_index);
    info.file->mark_local_available(block_info.target_index);
    --info.blocks_left;
    info.clone_queue.pop_front();
    info.sink = std::move(opened_file);
    process(it);
}

void controller_actor_t::process(write_it_t it) noexcept {
    auto &info = it->second;
    if (!info.clone_queue.empty()) {
        using request_t = fs::payload::clone_request_t;
        auto target_sz = static_cast<size_t>(info.file->get_size());
        auto &clone_block = info.clone_queue.front();
        auto block_sz = static_cast<size_t>(clone_block.block->get_size());
        auto &source = clone_block.source->get_path();
        auto source_offset = clone_block.source->get_block_offset(clone_block.source_index);
        auto target_offset = info.file->get_block_offset(clone_block.target_index);
        auto &path = info.file->get_path();
        request<request_t>(file_addr, source, path, target_sz, block_sz, source_offset, target_offset,
                           std::move(info.sink))
            .send(init_timeout);
    } else if (info.sink && !info.validated_blocks.empty()) {
        auto disk_view = info.sink->data();
        auto &blocks = info.validated_blocks;
        while (!blocks.empty()) {
            auto &res = blocks.front();
            auto &payload = res->payload;
            auto &req_payload = payload.req->payload.request_payload;
            auto &file = req_payload.file;
            auto &data = payload.res.data;
            auto block_index = req_payload.block.block_index();
            auto offset = file->get_block_offset(block_index);
            std::copy(data.begin(), data.end(), disk_view + offset);
            info.file->assign_block(req_payload.block.block(), block_index);
            --blocks_kept;
            blocks.pop_front();
        }
    }
    if (info.complete()) {
        assert(info.sink);
        request<fs::payload::close_request_t>(file_addr, std::move(info.sink), info.file->get_path())
            .send(init_timeout);
    } else if (state != r::state_t::OPERATIONAL && info.done()) {
        request<fs::payload::close_request_t>(file_addr, std::move(info.sink), info.file->get_path(), false)
            .send(init_timeout);
    }

    ready();
}
#endif

void controller_actor_t::on_validation(hasher::message::validation_response_t &res) noexcept {
    using namespace model::diff;
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

#if 0
void controller_actor_t::on_file_update(message::file_update_notify_t &message) noexcept {
    auto &file = message.payload.file;
    LOG_TRACE(log, "{}, on_file_update, {}", identity, file->get_full_name());
    if (state == r::state_t::OPERATIONAL) {
        send<payload::file_update_t>(peer_addr, file);
    }
}
#endif
