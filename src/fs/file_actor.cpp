#include "file_actor.h"
#include "net/names.h"
#include "model/diff/modify/append_block.h"
#include "model/diff/modify/clone_block.h"
#include "model/diff/modify/clone_file.h"
#include "model/diff/modify/flush_file.h"
#include "utils.h"

using namespace syncspirit::fs;

file_actor_t::file_actor_t(config_t &cfg) : r::actor_base_t{cfg}, cluster{cfg.cluster}, files_cache(cfg.mru_size) {
    log = utils::get_logger("fs.file_actor");
}

void file_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity("fs::file_actor", false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&file_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&file_actor_t::on_block_update, coordinator);
            }
        });
    });
}

void file_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    r::actor_base_t::on_start();
}

void file_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void file_actor_t::on_block_update(model::message::block_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_block_update", identity);
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

auto file_actor_t::reflect(model::file_info_ptr_t &file_ptr) noexcept -> outcome::result<void> {
    auto &file = *file_ptr;
    auto &path = file.get_path();
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
        if (file.is_locally_available() && sz) {
            return outcome::success();
        }

        bool temporal = sz > 0;
        if (temporal) {
            LOG_TRACE(log, "{}, touching file {} ({} bytes)", identity, path.string(), sz);
            auto file_opt = open_file(path, temporal, &file);
            if (!file_opt) {
                auto &err = file_opt.assume_error();
                LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path.string(), err.message());
                return err;
            }
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
            out.close();

            std::time_t modified = file.get_modified_s();
            bfs::last_write_time(path, modified, ec);
            if (ec) {
                return ec;
            }
        }
    } else if (file.is_dir()) {
        LOG_DEBUG(log, "{}, creating directory {}", identity, path.string());
        bfs::create_directory(path, ec);
        if (ec) {
            return ec;
        }
    } else if (file.is_link()) {
        auto target = bfs::path(file.get_link_target());
        LOG_DEBUG(log, "{}, creating symlink {} -> {}", identity, path.string(), target.string());

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

auto file_actor_t::operator()(const model::diff::modify::clone_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file.name());
    return reflect(file);
}

auto file_actor_t::operator()(const model::diff::modify::flush_file_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    assert(file->is_locally_available());

    auto path = file->get_path().string();
    auto mmaped_file = files_cache.get(path);
    if (!mmaped_file) {
        LOG_ERROR(log, "{}, attempt to flush non-opend file {}", identity, path);
        auto ec = sys::errc::make_error_code(sys::errc::io_error);
        return ec;
    }

    files_cache.remove(mmaped_file);
    auto ok = mmaped_file->close();
    if (!ok) {
        auto &ec = ok.assume_error();
        LOG_ERROR(log, "{}, cannot close file: {}: {}", identity, path, ec.message());
        return ec;
    }

    LOG_INFO(log, "{}, file {} ({} bytes) is now locally available", identity, path, file->get_size());
    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::append_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto file_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto file = file_info->get_file_infos().by_name(diff.file_name);
    auto &path = file->get_path();
    auto path_str = path.string();
    auto file_opt = open_file(path, true, file);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, path_str, err.message());
        return err;
    }

    auto &mmaped_file = file_opt.assume_value();
    auto disk_view = mmaped_file->data();
    auto &data = diff.data;
    auto block_index = diff.block_index;
    auto offset = file->get_block_offset(block_index);
    std::copy(data.begin(), data.end(), disk_view + offset);

    return outcome::success();
}

auto file_actor_t::operator()(const model::diff::modify::clone_block_t &diff) noexcept -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto target_folder_info = folder->get_folder_infos().by_device_id(diff.device_id);
    auto target = target_folder_info->get_file_infos().by_name(diff.file_name);

    auto source_folder_info = folder->get_folder_infos().by_device_id(diff.source_device_id);
    auto source = source_folder_info->get_file_infos().by_name(diff.source_file_name);

    auto &target_path = target->get_path();
    auto file_opt = open_file(target_path, true, target);
    if (!file_opt) {
        auto &err = file_opt.assume_error();
        LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, target_path.string(), err.message());
        return err;
    }
    auto target_mmap = std::move(file_opt.assume_value());
    auto source_mmap = mmaped_file_t::backend_t();

    auto source_path = source->get_path();
    if (!source->is_locally_available()) {
        source_path = make_temporal(source_path);
    }
    if (source_path == target_path) {
        source_mmap = target_mmap->get_backend();
    } else {
        bio::mapped_file_params params;
        params.path = source_path.string();
        params.flags = bio::mapped_file::mapmode::readonly;
        auto source_opt = open_file(source_path, params);
        if (!source_opt) {
            auto &ec = source_opt.assume_error();
            LOG_ERROR(log, "{}, cannot open file: {}: {}", identity, params.path, ec.message());
            return ec;
        }
        source_mmap = std::move(source_opt.assume_value());
    }

    auto target_view = target_mmap->data();
    auto &block = target->get_blocks()[diff.block_index];
    auto target_offset = target->get_block_offset(diff.block_index);
    auto source_view = source_mmap->const_data();
    auto source_offset = source->get_block_offset(diff.source_block_index);
    auto source_begin = source_view + source_offset;
    auto source_end = source_begin + block->get_size();
    std::copy(source_begin, source_end, target_view + target_offset);

    return outcome::success();
}

auto file_actor_t::open_file(const boost::filesystem::path &path, bool temporal, model::file_info_ptr_t info) noexcept
    -> outcome::result<mmaped_file_ptr_t> {
    auto item = files_cache.get(path.string());
    if (item) {
        return item;
    }

    auto size = info->get_size();
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

    auto &backend = backend_opt.assume_value();
    item = new mmaped_file_t(path, std::move(std::move(backend)), temporal, std::move(info));
    files_cache.put(item);
    return std::move(item);
}

auto file_actor_t::open_file(const boost::filesystem::path &path, const bio::mapped_file_params &params) noexcept
    -> outcome::result<mmaped_file_t::backend_t> {
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
