#include "fs_actor.h"
#include "../net/names.h"
#include "utils.h"
#include <spdlog/spdlog.h>

using namespace syncspirit::fs;

fs_actor_t::fs_actor_t(config_t &cfg) : r::actor_base_t{cfg}, fs_config{cfg.fs_config} {}

void fs_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(net::names::fs, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(net::names::fs, get_address()); });

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&fs_actor_t::on_scan_request);
        p.subscribe_actor(&fs_actor_t::on_scan);
    });
}

void fs_actor_t::on_start() noexcept {
    spdlog::debug("{}, on_start", identity);
    r::actor_base_t::on_start();
}

void fs_actor_t::shutdown_finish() noexcept {
    spdlog::debug("{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void fs_actor_t::on_scan_request(message::scan_request_t &req) noexcept {
    auto &root = req.payload.root;
    auto task = payload::scan_t{root, req.payload.reply_to, {}, {root}, {}, {}, {}};
    send<payload::scan_t>(address, std::move(task));
}

void fs_actor_t::on_scan(message::scan_t &req) noexcept {
    auto dirs_counter = fs_config.batch_dirs_count;
    auto &p = req.payload;
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
        send<payload::scan_response_t>(p.reply_to, std::move(p.root), std::move(p.file_map));
    }
}

void fs_actor_t::scan_dir(bfs::path &dir, payload::scan_t &payload) noexcept {
    if (!bfs::exists(dir)) {
        return;
    }
    if (!bfs::is_directory(dir)) {
        return;
    }

    for (auto it = bfs::directory_iterator(dir); it != bfs::directory_iterator(); ++it) {
        auto &child = *it;
        if (bfs::is_directory(child)) {
            payload.scan_dirs.push_back(child);
        } else if (bfs::is_regular_file(child)) {
            payload.files_queue.push_back(child);
        } else if (bfs::is_symlink(child)) {
            std::abort();
        }
    }
}

std::uint32_t fs_actor_t::calc_block(payload::scan_t &payload) noexcept {
    if (payload.next_block) {
        auto &block = payload.next_block.value();
        auto &file = payload.file_map[block.path];
        auto block_info = compute(block);
        auto recorded_info = payload.blocks_map.by_id(block_info->get_hash());
        if (recorded_info) {
            file.emplace_back(std::move(recorded_info));
        } else {
            file.emplace_back(block_info);
            payload.blocks_map.put(block_info);
        }

        // if it is the last block, reset the current block
        auto next_bytes = block.block_size * (block.block_index + 1);
        if (next_bytes >= block.file_size) {
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
            send<payload::scan_error_t>(payload.reply_to, payload.root, file, r.error());
        } else {
            payload.next_block = std::move(r.value());
        }
    }
    return 0;
}
