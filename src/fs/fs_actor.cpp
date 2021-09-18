#include "fs_actor.h"
#include "../net/names.h"
#include "../utils/error_code.h"
#include "../utils/tls.h"
#include "utils.h"
#include <fstream>

namespace sys = boost::system;
using namespace syncspirit::fs;

fs_actor_t::fs_actor_t(config_t &cfg) : r::actor_base_t{cfg}, fs_config{cfg.fs_config} {
    log = utils::get_logger("fs.actor");
}

void fs_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(net::names::fs, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(net::names::fs, get_address()); });

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&fs_actor_t::on_scan_request);
        p.subscribe_actor(&fs_actor_t::on_scan_cancel);
        p.subscribe_actor(&fs_actor_t::on_scan);
        p.subscribe_actor(&fs_actor_t::on_process);
        p.subscribe_actor(&fs_actor_t::on_open);
        p.subscribe_actor(&fs_actor_t::on_close);
        p.subscribe_actor(&fs_actor_t::on_clone);
    });
}

void fs_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void fs_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void fs_actor_t::on_scan_request(message::scan_request_t &req) noexcept {
    auto &root = req.payload.root;
    LOG_TRACE(log, "{}, on_scan_request, root = {}", identity, root.c_str());

    queue.emplace_back(&req);
    if (queue.size() == 1) {
        process_queue();
    }
}

void fs_actor_t::on_scan_cancel(message::scan_cancel_t &req) noexcept {
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

void fs_actor_t::on_process(message::process_signal_t &) noexcept { process_queue(); }

void fs_actor_t::process_queue() noexcept {
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

void fs_actor_t::reply(message::scan_t &scan) noexcept {
    auto &p = scan.payload;
    auto &payload = p.request->payload;
    auto &requestee = payload.reply_to;
    auto &root = payload.root;
    send<payload::scan_response_t>(requestee, std::move(p.file_map));
    queue.pop_front();
    send<payload::process_signal_t>(address);
}

void fs_actor_t::on_scan(message::scan_t &req) noexcept {
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

    auto blocks_size = fs_config.batch_block_size;
    auto bs_condition = [&]() { return (blocks_size > 0) && (p.next_block || !p.files_queue.empty()); };
    while (bs_condition()) {
        auto bytes = calc_block(p);
        blocks_size -= std::min(bytes, blocks_size);
    }
    if (blocks_size == 0) {
        send<payload::scan_t>(address, std::move(p));
        return;
    }
    if (p.scan_dirs.empty()) {
        reply(req);
    }
}

void fs_actor_t::scan_dir(bfs::path &dir, payload::scan_t &payload) noexcept {
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

std::uint32_t fs_actor_t::calc_block(payload::scan_t &payload) noexcept {
    if (payload.next_block) {
        auto &block = payload.next_block.value();
        auto &container = payload.file_map->map;
        auto &root = payload.request->payload.root;
        auto rel_path = syncspirit::fs::relative(block.path, root);
        auto &local_info = container[rel_path.path];
        auto block_info = compute(block);
        auto recorded_info = payload.blocks_map.by_id(block_info->get_hash());
        if (recorded_info) {
            local_info.blocks.emplace_back(std::move(recorded_info));
        } else {
            local_info.blocks.emplace_back(block_info);
            payload.blocks_map.put(block_info);
        }

        // if it is the last block, reset the current block
        auto next_bytes = block.block_size * (block.block_index + 1);
        if (next_bytes >= block.file_size) {
            local_info.temp = rel_path.temp;
            payload.next_block.reset();
        } else {
            block.block_index++;
        }

        return block_info->get_size();
    } else {
        assert(!payload.files_queue.empty());
        auto file = payload.files_queue.front();
        payload.files_queue.pop_front();
        auto r = prepare(file);
        if (!r) {
            auto &p = payload.request->payload;
            auto &requestee = p.reply_to;
            auto &root = p.root;
            send<payload::scan_error_t>(requestee, root, file, r.error());
        } else {
            payload.next_block = std::move(r.value());
        }
    }
    return 0;
}

void fs_actor_t::on_open(message::open_request_t &req) noexcept {
    auto &payload = req.payload.request_payload;
    LOG_TRACE(log, "{}, on_open, path = {}", identity, payload.path.string());
    auto path = make_temporal(payload.path);
    auto parent = path.parent_path();
    sys::error_code ec;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return reply_with_error(req, make_error(ec));
        }
    }

    bio::mapped_file_params params;
    params.path = path.string();
    params.flags = bio::mapped_file::mapmode::readwrite;
    if (!bfs::exists(path)) {
        params.new_file_size = payload.file_size;
    }

    auto file = std::make_unique<typename opened_file_t::element_type>();
    try {
        file->open(params);
    } catch (const std::exception &ex) {
        LOG_ERROR(log, "{}, error opening file {}: {}", identity, params.path, ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        auto ee = make_error(ec);
        reply_with_error(req, ee);
        return;
    }
    reply_to(req, std::move(file));
}

void fs_actor_t::on_close(message::close_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_close", identity);
    auto &payload = *req.payload.request_payload;
    auto &file = payload.file;
    auto &path = payload.path;

    try {
        file->close();
    } catch (const std::exception &ex) {
        LOG_ERROR(log, "{}, error closing file {}: {}", identity, path.string(), ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        auto ee = make_error(ec);
        reply_with_error(req, ee);
        return;
    }

    sys::error_code ec;
    auto tmp_path = make_temporal(payload.path);
    bfs::rename(tmp_path, path, ec);
    if (ec) {
        return reply_with_error(req, make_error(ec));
    }
    reply_to(req);
}

void fs_actor_t::on_clone(message::clone_request_t &req) noexcept {
    auto &p = req.payload.request_payload;
    LOG_TRACE(log, "{}, on_clone, from {} (off: {}) {} bytes", identity, p.source.string(), p.source_offset,
              p.block_size);

    bio::mapped_file_params s_params;
    s_params.path = p.source.string();
    s_params.flags = bio::mapped_file::mapmode::readonly;

    auto target = opened_file_t();
    if (p.target_file) {
        target = std::move(p.target_file);
    } else {
        bio::mapped_file_params params;
        params.path = p.target.string();
        params.flags = bio::mapped_file::mapmode::readwrite;
        if (!bfs::exists(p.target)) {
            params.new_file_size = p.target_size;
        }
        try {
            target = std::make_unique<bio::mapped_file>(params);
        } catch (std::exception &ex) {
            LOG_TRACE(log, "{}, on_clone, error opening {} : {}", identity, p.target.string(), ex.what());
            auto ec = sys::errc::make_error_code(sys::errc::io_error);
            auto ee = make_error(ec);
            reply_with_error(req, ee);
            return;
        }
    }

    try {
        auto source = bio::mapped_file(s_params);
        auto begin = source.const_data() + p.source_offset;
        auto end = begin + p.block_size;
        auto to = target->data() + p.target_offset;
        std::copy(begin, end, to);
        reply_to(req, std::move(target));
    } catch (std::exception &ex) {
        LOG_TRACE(log, "{}, on_clone, error cloing from {} (off: {}) {} bytes : {}", identity, p.source.string(),
                  p.source_offset, p.block_size, ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        auto ee = make_error(ec);
        reply_with_error(req, ee);
        return;
    }
}
