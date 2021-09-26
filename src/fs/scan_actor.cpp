#include "scan_actor.h"
#include "../net/names.h"
#include "../utils/error_code.h"
#include "../utils/tls.h"
#include "utils.h"
#include <fstream>

namespace sys = boost::system;
using namespace syncspirit::fs;

scan_actor_t::scan_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, fs_config{cfg.fs_config}, hasher_proxy{cfg.hasher_proxy}, requested_hashes_limit{
                                                                                          cfg.requested_hashes_limit} {
    log = utils::get_logger("scan::actor");
}

void scan_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>(
        [&](auto &p) { p.set_identity(net::names::scan_actor, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(net::names::scan_actor, get_address()); });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(hasher_proxy, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&scan_actor_t::on_scan_request);
        p.subscribe_actor(&scan_actor_t::on_scan_cancel);
        p.subscribe_actor(&scan_actor_t::on_scan);
        p.subscribe_actor(&scan_actor_t::on_process);
        p.subscribe_actor(&scan_actor_t::on_hash);
    });
}

void scan_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void scan_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void scan_actor_t::on_scan_request(message::scan_request_t &req) noexcept {
    auto &root = req.payload.root;
    LOG_TRACE(log, "{}, on_scan_request, root = {}", identity, root.c_str());

    queue.emplace_back(&req);
    if (queue.size() == 1) {
        process_queue();
    }
}

void scan_actor_t::on_scan_cancel(message::scan_cancel_t &req) noexcept {
    LOG_TRACE(log, "{}, on_scan_cancel", identity);
    auto req_id = req.payload.request_id;
    if (queue.size()) {
        auto it = queue.begin();
        if ((*it)->payload.request_id == req_id) {
            LOG_DEBUG(log, "{}, on_scan_cancel, cancelling ongoing scan for {}", identity,
                      queue.front()->payload.root.c_str());
            scan_cancelled = true;
            return;
        }
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if ((*it)->payload.request_id == req_id) {
                auto &payload = (*it)->payload;
                auto &root = payload.root;
                LOG_DEBUG(log, "{}, on_scan_cancel, cancelling pending scan for {}", root.c_str());
                auto &requestee = payload.reply_to;
                auto file_map = model::local_file_map_ptr_t(new model::local_file_map_t(root));
                file_map->ec = utils::make_error_code(utils::error_code_t::scan_aborted);
                send<payload::scan_response_t>(requestee, std::move(file_map));
                queue.erase(it);
                break;
            }
        }
    }
}

void scan_actor_t::on_process(message::process_signal_t &) noexcept { process_queue(); }

void scan_actor_t::process_queue() noexcept {
    if (queue.empty()) {
        LOG_TRACE(log, "{} empty queue, nothing to process", identity);
        return;
    }
    auto &req = queue.front();
    auto &root = req->payload.root;
    auto file_map = std::make_unique<model::local_file_map_t>(root);
    auto task = payload::scan_t{req, {}, {root}, {}, std::move(file_map), {}};
    send<payload::scan_t>(address, std::move(task));
}

void scan_actor_t::reply(message::scan_t &scan) noexcept {
    auto &p = scan.payload;
    auto &payload = p.request->payload;
    auto &requestee = payload.reply_to;
    auto &root = payload.root;
    send<payload::scan_response_t>(requestee, std::move(p.file_map));
    queue.pop_front();
    send<payload::process_signal_t>(address);
}

void scan_actor_t::on_scan(message::scan_t &req) noexcept {
    auto &p = req.payload;
    if (scan_cancelled) {
        scan_cancelled = true;
        p.file_map->ec = utils::make_error_code(utils::error_code_t::scan_aborted);
        return reply(req);
    }
    auto dirs_counter = fs_config.batch_dirs_count;
    while ((dirs_counter > 0) && (!p.scan_dirs.empty())) {
        --dirs_counter;
        auto &dir = p.scan_dirs.front();
        scan_dir(dir, p);
        p.scan_dirs.pop_front();
    }
    if (dirs_counter == 0) {
        send<payload::scan_t>(address, std::move(p));
        return;
    }

    calc_blocks(req);
}

void scan_actor_t::scan_dir(bfs::path &dir, payload::scan_t &payload) noexcept {
    sys::error_code ec;
    bool ok = bfs::exists(dir, ec);
    if (ec || !ok) {
        return;
    }
    ok = bfs::is_directory(dir, ec);
    if (ec || !ok) {
        return;
    }

    auto &p = payload.request->payload;
    auto &container = payload.file_map->map;
    if (dir != p.root) {
        auto rel_path = syncspirit::fs::relative(dir, p.root);
        container[rel_path.path].file_type = model::local_file_t::file_type_t::dir;
    }

    for (auto it = bfs::directory_iterator(dir); it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        bool is_dir = bfs::is_directory(child, ec);
        if (!ec && is_dir) {
            payload.scan_dirs.push_back(child);
        } else {
            bool is_reg = bfs::is_regular_file(child, ec);
            if (!ec && is_reg) {
                auto &child_path = child.path();
                if (!is_temporal(child_path)) {
                    payload.files_queue.push_back(child_path);
                } else {
                    auto modified_at = bfs::last_write_time(child_path, ec);
                    if (ec) {
                        send<payload::scan_error_t>(p.reply_to, p.root, child_path, ec);
                    } else {
                        auto now = std::time(nullptr);
                        if (modified_at + fs_config.temporally_timeout <= now) {
                            LOG_DEBUG(log, "{}, removing outdated temporally {}", child_path.string());
                            bfs::remove(child_path, ec);
                            if (ec) {
                                send<payload::scan_error_t>(p.reply_to, p.root, child_path, ec);
                            }
                        } else {
                            payload.files_queue.push_back(child_path);
                        }
                    }
                }
            } else {
                auto rel_path = syncspirit::fs::relative(child, p.root);
                bool is_sim = bfs::is_symlink(child, ec);
                if (!ec && is_sim) {
                    auto target = bfs::read_symlink(child, ec);
                    if (!ec) {
                        auto &local_info = container[rel_path.path];
                        local_info.file_type = model::local_file_t::file_type_t::symlink;
                        local_info.symlink_target = target;
                    }
                    continue;
                }
            }
        }
    }
}

void scan_actor_t::calc_blocks(message::scan_t &req) noexcept {
    auto &p = req.payload;
    while (requested_hashes < requested_hashes_limit && !scan_cancelled && state == r::state_t::OPERATIONAL) {
        if (!p.current_file.get() && p.files_queue.empty()) {
            if (!requested_hashes) {
                reply(req);
            }
            return;
        }
        if (!p.current_file) {
            bio::mapped_file_params params;
            auto &path = p.files_queue.front();
            auto &root = p.request->payload.root;
            params.path = path.string();
            params.flags = bio::mapped_file::mapmode::readonly;

            try {
                auto rel_path = syncspirit::fs::relative(path, root);
                auto file = payload::file_ptr_t(new payload::file_t(path, rel_path.path, rel_path.temp));
                auto file_sz = bfs::file_size(path);
                auto block_size = get_block_size(file_sz);
                file->mapped_file.open(params);
                file->file_size = file_sz;
                file->next_block_idx = 0;
                file->next_block_sz = block_size.first;
                file->blocks.resize(block_size.second);
                p.current_file = std::move(file);
            } catch (const std::exception &ex) {
                LOG_ERROR(log, "{}, error opening file {}: {}", identity, params.path, ex.what());
                auto ec = sys::errc::make_error_code(sys::errc::io_error);
                auto &requestee = p.request->payload.reply_to;
                p.current_file.reset();
                send<payload::scan_error_t>(requestee, root, path, ec);
            }
            p.files_queue.pop_front();
            continue;
        }
        auto &file = *p.current_file;
        if (file.file_size && (file.procesed_sz == file.file_size)) {
            p.current_file.reset();
            continue;
        }
        auto index = file.next_block_idx;
        auto left = file.file_size - (index * file.next_block_sz);
        auto block_sz = left > file.next_block_sz ? file.next_block_sz : left;

        using request_t = hasher::payload::digest_request_t;
        auto data = file.mapped_file.const_data();
        auto offset = index ? index * file.next_block_sz : 0;
        auto block = std::string_view(data + offset, block_sz);
        auto raw_block = new payload::block_task_t(p.current_file, index, file.next_block_sz, &req);
        auto block_task = payload::block_task_ptr_t(raw_block);
        request<request_t>(hasher_proxy, block, block_task.get()).send(init_timeout);
        intrusive_ptr_add_ref(&req);
        p.block_task_map[p.current_file.get()].emplace(std::move(block_task));

        ++file.next_block_idx;
        file.procesed_sz += block_sz;
        ++requested_hashes;
    }
    if (!requested_hashes) {
        reply(req);
    }
}

void scan_actor_t::on_hash(hasher::message::digest_response_t &res) noexcept {
    --requested_hashes;
    auto ptr = res.payload.req->payload.request_payload.custom;
    auto block_task = (payload::block_task_t *)(ptr);
    auto scan = (message::scan_t *)block_task->backref;
    auto &blocks = scan->payload.block_task_map[block_task->file.get()];
    auto block_ptr = payload::block_task_ptr_t(block_task);
    auto file = block_task->file;

    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        LOG_ERROR(log, "{}, on_hash, file: {}, error: {}", identity, file->path.string(), ee->message());
        intrusive_ptr_release(scan);
        return do_shutdown(ee);
    } else {
        auto &p = res.payload.res;
        auto &blocks_map = scan->payload.blocks_map;
        auto block = blocks_map.by_id(p.digest);
        if (!block) {
            db::BlockInfo info;
            info.set_hash(p.digest);
            info.set_weak_hash(p.weak);
            info.set_size(block_task->block_sz);
            block = model::block_info_ptr_t(new model::block_info_t(info));
            blocks_map.put(block);
        }
        file->blocks[block_task->block_idx] = block;
    }
    blocks.erase(block_ptr);
    if (blocks.size() == 0) {
        auto &container = scan->payload.file_map->map;
        auto &local_info = container[file->rel_path];
        local_info.blocks = std::move(file->blocks);
        local_info.file_type = model::local_file_t::file_type_t::regular;
        local_info.temp = file->temp;
    }
    if (!scan_cancelled) {
        calc_blocks(*scan);
    }
    intrusive_ptr_release(scan);
}
