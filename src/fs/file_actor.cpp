#include "file_actor.h"
#include "../net/names.h"
#include "utils.h"

using namespace syncspirit::fs;

file_actor_t::file_actor_t(config_t &cfg) : r::actor_base_t{cfg} { log = utils::get_logger(net::names::file_actor); }

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>(
        [&](auto &p) { p.set_identity(net::names::file_actor, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(net::names::file_actor, address); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&file_actor_t::on_open);
        p.subscribe_actor(&file_actor_t::on_close);
        p.subscribe_actor(&file_actor_t::on_clone);
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

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

void file_actor_t::on_close(message::close_request_t &req) noexcept {
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
