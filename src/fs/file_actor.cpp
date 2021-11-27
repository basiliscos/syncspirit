#include "file_actor.h"
#include "net/names.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "utils.h"

using namespace syncspirit::fs;

file_actor_t::file_actor_t(config_t &cfg) : r::actor_base_t{cfg} { log = utils::get_logger(net::names::file_actor); }

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>(
        [&](auto &p) { p.set_identity(net::names::file_actor, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::file_actor, address);
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&file_actor_t::on_block_update, coordinator);
            }
        });
    });

    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
#if 0
        p.subscribe_actor(&file_actor_t::on_open);
        p.subscribe_actor(&file_actor_t::on_close);
        p.subscribe_actor(&file_actor_t::on_clone);
#endif
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void file_actor_t::on_block_update(net::message::block_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_block_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    //auto file_opt = open_file(file->get_path());

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::clone_block_t &diff) noexcept -> outcome::result<void> {
    return outcome::success();
}

auto file_actor_t::open_file(bfs::path path, bool temporal, size_t size) noexcept -> outcome::result<opened_file_t> {
    LOG_TRACE(log, "{}, on_open, path = {} ({} bytes)", identity, path.string());
    if (temporal) {
        path = make_temporal(path);
    }
    auto parent = path.parent_path();
    sys::error_code ec;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    bio::mapped_file_params params;
    params.path = path.string();
    params.flags = bio::mapped_file::mapmode::readwrite;

    auto file = std::make_unique<typename opened_file_t::element_type>();
    try {
        if (!bfs::exists(path) || bfs::file_size(path) != size) {
            params.new_file_size = size;
        }
        file->open(params);
    } catch (const std::exception &ex) {
        LOG_ERROR(log, "{}, error opening file {}: {}", identity, params.path, ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        return ec;
    }
    return std::move(file);
}



#if 0
void file_actor_t::on_open(message::open_request_t &req) noexcept {
    auto &payload = req.payload.request_payload;
    LOG_TRACE(log, "{}, on_open, path = {} ({} bytes)", identity, payload.path.string(), payload.file_size);
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

    auto file = std::make_unique<typename opened_file_t::element_type>();
    try {
        if (!bfs::exists(path) || bfs::file_size(path) != payload.file_size) {
            params.new_file_size = payload.file_size;
        }

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

void file_actor_t::on_close(message::close_request_t &req) noexcept {
    LOG_TRACE(log, "{}, on_close", identity);
    auto &payload = *req.payload.request_payload;
    auto &file = payload.file;
    auto &path = payload.path;

    assert(file);
    try {
        file->close();
    } catch (const std::exception &ex) {
        LOG_ERROR(log, "{}, error closing file {}: {}", identity, path.string(), ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        auto ee = make_error(ec);
        reply_with_error(req, ee);
        return;
    }

    if (payload.complete) {
        sys::error_code ec;
        auto tmp_path = make_temporal(payload.path);
        bfs::rename(tmp_path, path, ec);
        if (ec) {
            return reply_with_error(req, make_error(ec));
        }
    }
    reply_to(req);
}

void file_actor_t::on_clone(message::clone_request_t &req) noexcept {
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
        auto target_path = make_temporal(p.target);
        params.path = target_path.string();
        params.flags = bio::mapped_file::mapmode::readwrite;
        if (!bfs::exists(target_path)) {
            params.new_file_size = p.target_size;
        }
        try {
            target = std::make_unique<bio::mapped_file>(params);
        } catch (std::exception &ex) {
            LOG_TRACE(log, "{}, on_clone, error opening {} : {}", identity, target_path.string(), ex.what());
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
#endif
