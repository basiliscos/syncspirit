#include "file_actor.h"
#include "net/names.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/new_file.h"
#include "utils.h"

using namespace syncspirit::fs;

file_actor_t::file_actor_t(config_t &cfg) : r::actor_base_t{cfg}, cluster{cfg.cluster}, files_cache(cfg.mru_size) { log = utils::get_logger(net::names::file_actor); }

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
                plugin->subscribe_actor(&file_actor_t::on_model_update, coordinator);
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

void file_actor_t::on_model_update(net::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
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

auto file_actor_t::reflect(const model::file_info_t& file) noexcept -> outcome::result<void> {
    auto& path = file.get_path();
    sys::error_code ec;

    if (file.is_deleted()) {
        if (bfs::exists(path, ec)) {
            LOG_DEBUG(log, "{} removing {}", identity, path.string());
            auto ok = bfs::remove_all(path, ec);
            if (!ok) {
                LOG_ERROR(log, "{},  error removing {} : {}", identity, path.string(), ec.message());
                return ec;

            }
        } else {
            LOG_TRACE(log, "{}, {} already abscent, noop", identity, path.string());
        }
        return outcome::success();
    }

    auto parent = path.parent_path();

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    if (file.is_file()) {
        auto sz = file.get_size();
        bool temporal = sz > 0;
        if (temporal) {
            LOG_TRACE(log, "{}, touching file {} ({} bytes)", identity, path.string(), sz);
            auto file_opt = open_file(path, temporal, sz);
            if (!file_opt) {
                auto& err = file_opt.assume_error();
                LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path.string(), err.message());
                return err;
            }
            auto& f = file_opt.assume_value();
        } else {
            LOG_TRACE(log, "{}, touching empty file {}", identity, path.string());
            std::ofstream out;
            out.exceptions(out.failbit | out.badbit);
            try {
                out.open(path.string());
            } catch (const std::ios_base::failure &e) {
                LOG_ERROR(log, "{}, error creating {} : {}", identity, path.string(), e.code().message());
                return sys::errc::make_error_code(sys::errc::io_error);
            }
        }
    }
    else if (file.is_dir()) {
        LOG_TRACE(log, "{}, creating directory {}", identity, path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            return ec;
        }
    } else if (file.is_link()) {
        auto target = bfs::path(file.get_link_target());
        LOG_TRACE(log, "{}, creating symlink {} -> {}", identity, path.string(), target.string());

        bool attempt_create =
            !bfs::exists(path, ec) || !bfs::is_symlink(path, ec) || (bfs::read_symlink(path, ec) != target);
        if (attempt_create) {
            bfs::create_symlink(target, path, ec);
            if (ec) {
                LOG_WARN(log, "{}, error symlinking {} -> {} {} : {}", identity, path.string(), target.string(),
                         ec.message());
                return ec;
            }
        } else {
            LOG_TRACE(log, "{}, no need to create symlink {} -> {}", identity, path.string(), target.string());
        }
    }

    return outcome::success();
}


auto file_actor_t::operator()(const model::diff::modify::new_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto file = file_info->get_file_infos().by_name(diff.file.name());
    return reflect(*file);
}


auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    auto& path = file->get_path();
    auto& path_str = path.string();
    auto file_opt = open_file(path, true, file->get_size());
    if (!file_opt) {
        auto& err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path_str, err.message());
        return err;
    }


    auto& mmaped_file = file_opt.assume_value();
    auto disk_view = mmaped_file->data();
    auto &data = diff.data;
    auto block_index = diff.block_index;
    auto offset = file->get_block_offset(block_index);
    std::copy(data.begin(), data.end(), disk_view + offset);

    if (file->is_locally_available()) {
        files_cache.remove(mmaped_file);
        auto ok = mmaped_file->close();
        if (!ok) {
            auto& ec = ok.assume_error();
            LOG_ERROR(log, "{}, cannot close file (after appending block): {}: {}", identity, path.string(), ec.message());
            return ec;
        }
        LOG_INFO(log, "{}, file {} is now locally available", identity, path_str);
    }

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::clone_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.target_folder_id);
    auto target_folder_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto target = target_folder_info->get_file_infos().by_name(diff.target_file_name);

    auto source_folder_info = folder->get_folder_infos().by_device_id(diff.source_device_id);
    auto source = source_folder_info->get_file_infos().by_name(diff.source_file_name);

    auto& target_path = target->get_path();
    auto file_opt = open_file(target_path, true, target->get_size());
    if (!file_opt) {
        auto& err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, target_path.string(), err.message());
        return err;
    }
    auto target_mmap = std::move(file_opt.assume_value());
    auto source_mmap = mmaped_file_t::backend_t();

    auto& source_path = source->get_path();
    if (source_path == target_path) {
        source_mmap = target_mmap->get_backend();
    } else {
        bio::mapped_file_params params;
        params.path = source_path.string();
        params.flags = bio::mapped_file::mapmode::readonly;
        auto source_opt = open_file(source_path, params);
        if (!source_opt) {
            auto& ec = source_opt.assume_error();
            LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, params.path, ec.message());
            return ec;
        }
        source_mmap = std::move(source_opt.assume_value());
    }

    auto target_view = target_mmap->data();
    auto& block = target->get_blocks()[diff.target_block_index];
    auto target_offset = target->get_block_offset(diff.target_block_index);
    auto source_view = source_mmap->const_data();
    auto source_offset = source->get_block_offset(diff.source_block_index);
    auto source_begin = source_view + source_offset;
    auto source_end = source_begin + block->get_size();
    std::copy(source_begin, source_end, target_view + target_offset);

    if (target->is_locally_available()) {
        files_cache.remove(target_mmap);
        auto ok = target_mmap->close();
        if (!ok) {
            auto& ec = ok.assume_error();
            LOG_ERROR(log, "{}, cannot close file (after appending block): {}: {}", identity, target_path.string(), ec.message());
            return ec;
        }
        LOG_INFO(log, "{}, file {} is now locally available", identity, target_path.string());
    }

    return outcome::success();
}

auto file_actor_t::open_file(const boost::filesystem::path &path, bool temporal, size_t size) noexcept -> outcome::result<mmaped_file_ptr_t> {
    auto item = files_cache.get(path.string());
    if (item) {
        return item;
    }

    LOG_TRACE(log, "{}, open_file, path = {} ({} bytes)", identity, path.string(), size);
    bfs::path operational_path = temporal ? make_temporal(path) : path;

    auto parent = operational_path.parent_path();
    sys::error_code ec;

    bool exists = bfs::exists(parent, ec);
    if (!exists) {
        bfs::create_directories(parent, ec);
        if (ec) {
            return ec;
        }
    }

    bio::mapped_file_params params;
    params.path = operational_path.string();
    params.flags = bio::mapped_file::mapmode::readwrite;
    if (!bfs::exists(operational_path, ec) || bfs::file_size(operational_path, ec) != size) {
        params.new_file_size = size;
    }

    auto backend_opt = open_file(path, params);
    if (!backend_opt) {
        return backend_opt.assume_error();
    }

    auto& backend = backend_opt.assume_value();
    item = new mmaped_file_t(path, std::move(std::move(backend)), temporal);
    files_cache.put(item);
    return std::move(item);
}


auto file_actor_t::open_file(const boost::filesystem::path &path, const bio::mapped_file_params& params) noexcept -> outcome::result<mmaped_file_t::backend_t> {
    auto file = mmaped_file_t::backend_t(new bio::mapped_file());
    try {
        file->open(params);
    } catch (const std::exception &ex) {
        LOG_ERROR(log, "{}, error opening file {}: {}", identity, params.path, ex.what());
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        return ec;
    }
    return outcome::success(std::move(file));
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
