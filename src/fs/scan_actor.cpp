// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "scan_actor.h"
#include "model/diff/modify/file_availability.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/modify/blocks_availability.h"
#include "model/diff/modify/new_file.h"
#include "net/names.h"
#include "utils/error_code.h"
#include "utils/tls.h"
#include "utils.h"
#include <fstream>
#include <algorithm>

namespace sys = boost::system;
using namespace syncspirit::fs;

template <class> inline constexpr bool always_false_v = false;

scan_actor_t::scan_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, cluster{cfg.cluster}, fs_config{cfg.fs_config}, hasher_proxy{cfg.hasher_proxy},
      requested_hashes_limit{cfg.requested_hashes_limit} {
    log = utils::get_logger("scan::actor");
}

void scan_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("fs::scan_actor", false);
        new_files = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.discover_name(net::names::coordinator, coordinator, true).link(false); });
    plugin.with_casted<r::plugin::link_client_plugin_t>([&](auto &p) { p.link(hasher_proxy, false); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&scan_actor_t::on_scan);
        p.subscribe_actor(&scan_actor_t::on_initiate_scan);
        p.subscribe_actor(&scan_actor_t::on_hash);
        p.subscribe_actor(&scan_actor_t::on_hash_new, new_files);
        p.subscribe_actor(&scan_actor_t::on_rehash);
        p.subscribe_actor(&scan_actor_t::on_hash_anew);
    });
}

void scan_actor_t::on_start() noexcept {
    LOG_TRACE(log, "{}, on_start", identity);
    for (auto it : cluster->get_folders()) {
        send<payload::scan_folder_t>(address, std::string(it.item->get_id()));
    }
    r::actor_base_t::on_start();
}

void scan_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "{}, shutdown_finish", identity);
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

void scan_actor_t::initiate_scan(std::string_view folder_id) noexcept {
    LOG_DEBUG(log, "{}, initiating scan of {}", identity, folder_id);
    auto task = scan_task_ptr_t(new scan_task_t(cluster, folder_id, fs_config));
    send<payload::scan_progress_t>(address, std::move(task));
}

void scan_actor_t::on_initiate_scan(message::scan_folder_t &message) noexcept {
    if (!progress) {
        ++progress;
        initiate_scan(message.payload.folder_id);
    } else {
        queue.emplace_back(&message);
    }
}

void scan_actor_t::process_queue() noexcept {
    if (!queue.empty() && state == r::state_t::OPERATIONAL) {
        auto &msg = queue.front();
        initiate_scan(msg->payload.folder_id);
        queue.pop_front();
    }
}

void scan_actor_t::on_scan(message::scan_progress_t &message) noexcept {
    auto &task = message.payload.task;
    auto folder_id = task->get_folder_id();
    LOG_TRACE(log, "{}, on_scan, folder = {}", identity, folder_id);

    auto r = task->advance();
    bool stop_processing = false;
    bool completed = false;
    std::visit(
        [&](auto &&r) {
            using T = std::decay_t<decltype(r)>;
            if constexpr (std::is_same_v<T, bool>) {
                stop_processing = !r;
                if (stop_processing) {
                    completed = true;
                }
            } else if constexpr (std::is_same_v<T, scan_errors_t>) {
                send<model::payload::io_error_t>(coordinator, std::move(r));
            } else if constexpr (std::is_same_v<T, unchanged_meta_t>) {
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::file_availability_t(r.file);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            } else if constexpr (std::is_same_v<T, changed_meta_t>) {
                LOG_WARN(log, "{}, changes in '{}' are ignored (not implemented)", identity, r.file->get_full_name());
            } else if constexpr (std::is_same_v<T, unknown_file_t>) {
                auto errs = initiate_hash(task, r.path);
                if (errs.empty()) {
                    stop_processing = true;
                } else {
                    send<model::payload::io_error_t>(coordinator, std::move(errs));
                }
            } else if constexpr (std::is_same_v<T, incomplete_t>) {
                auto errs = initiate_rehash(task, r.file);
                if (errs.empty()) {
                    stop_processing = true;
                } else {
                    send<model::payload::io_error_t>(coordinator, std::move(errs));
                }
            } else if constexpr (std::is_same_v<T, incomplete_removed_t>) {
                auto &file = *r.file;
                if (file.is_locked()) {
                    auto diff = model::diff::cluster_diff_ptr_t{};
                    diff = new model::diff::modify::lock_file_t(file, false);
                    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
                }
            } else if constexpr (std::is_same_v<T, file_error_t>) {
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::lock_file_t(*r.file, false);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
                auto path = make_temporal(r.file->get_path());
                scan_errors_t errors{scan_error_t{path, r.ec}};
                send<model::payload::io_error_t>(coordinator, std::move(errors));
            } else {
                static_assert(always_false_v<T>, "non-exhaustive visitor!");
            }
        },
        r);

    if (!stop_processing) {
        send<payload::scan_progress_t>(address, std::move(task));
    } else if (completed) {
        LOG_DEBUG(log, "{}, completed scanning of {}", identity, folder_id);
        --progress;
        process_queue();
    }
}

auto scan_actor_t::initiate_rehash(scan_task_ptr_t task, model::file_info_ptr_t file) noexcept -> model::io_errors_t {
    auto orig_path = file->get_path();
    auto path = make_temporal(orig_path);
    auto opt = file_t::open_read(path);
    if (!opt) {
        auto &ec = opt.assume_error();
        LOG_WARN(log, "{}, error opening file {}: {}", identity, path.string(), ec.message());
        model::io_errors_t errs;
        errs.push_back(model::io_error_t{orig_path, ec});
        if (ec) {
            errs.push_back(model::io_error_t{orig_path, ec});
        }
        return errs;
    }

    assert(file->get_source());
    auto file_ptr = file_ptr_t(new file_t(std::move(opt.value())));
    send<payload::rehash_needed_t>(address, std::move(task), std::move(file), file->get_source(), std::move(file_ptr));
    return {};
}

auto scan_actor_t::initiate_hash(scan_task_ptr_t task, const bfs::path &path) noexcept -> model::io_errors_t {
    auto opt = file_t::open_read(path);
    if (!opt) {
        auto &ec = opt.assume_error();
        LOG_WARN(log, "{}, error opening file {}: {}", identity, path.string(), ec.message());
        model::io_errors_t errs;
        errs.push_back(model::io_error_t{path, ec});
        return errs;
    }

    auto file_ptr = file_ptr_t(new file_t(std::move(opt.value())));
    send<payload::hash_anew_t>(address, std::move(task), std::move(file_ptr));
    return {};
}

void scan_actor_t::on_rehash(message::rehash_needed_t &message) noexcept {
    LOG_TRACE(log, "{}, on_rehash", identity);
    hash_next(message, address);
    if (requested_hashes < requested_hashes_limit) {
        process_queue();
    }
}

void scan_actor_t::on_hash_anew(message::hash_anew_t &message) noexcept {
    LOG_TRACE(log, "{}, on_hash_anew", identity);
    auto initial = requested_hashes;
    hash_next(message, new_files);
    // file w/o context
    if (initial == requested_hashes) {
        commit_new_file(message.payload);
    }
    if (requested_hashes < requested_hashes_limit) {
        process_queue();
    }
}

template <typename Message>
void scan_actor_t::hash_next(Message &message, const r::address_ptr_t &reply_addr) noexcept {
    auto &info = message.payload;
    if (info.has_more_chunks()) {
        auto condition = [&]() { return requested_hashes < requested_hashes_limit && info.has_more_chunks(); };
        while (condition()) {
            auto opt = info.read();
            if (!opt) {
                auto ec = opt.assume_error();
                model::io_errors_t errs;
                errs.push_back(model::io_error_t{info.get_path(), ec});
                send<model::payload::io_error_t>(coordinator, std::move(errs));
            } else {
                auto &chunk = opt.assume_value();
                if (chunk.data.size()) {
                    using request_t = hasher::payload::digest_request_t;
                    request_via<request_t>(hasher_proxy, reply_addr, std::move(chunk.data), (size_t)chunk.block_index,
                                           r::message_ptr_t(&message))
                        .send(init_timeout);
                    ++requested_hashes;
                }
            }
        }
    }
    return;
}

void scan_actor_t::on_hash(hasher::message::digest_response_t &res) noexcept {
    --requested_hashes;

    auto &rp = res.payload.req->payload.request_payload;
    auto msg = static_cast<message::rehash_needed_t *>(rp.custom.get());
    auto &info = msg->payload;
    info.ack_hashing();

    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        auto file = info.get_file();
        LOG_ERROR(log, "{}, on_hash, file: {}, block = {}, error: {}", identity, file->get_full_name(), rp.block_index,
                  ee->message());
        return do_shutdown(ee);
    }

    bool queued_next = false;
    if (info.is_valid()) {
        auto &digest = res.payload.res.digest;
        auto block_index = rp.block_index;
        if (info.ack_block(digest, block_index)) {
            hash_next(*msg, address);
            bool can_process_more = requested_hashes < requested_hashes_limit;
            queued_next = !can_process_more;
            if (info.is_complete()) {
                auto valid_blocks = info.has_valid_blocks();
                if (valid_blocks >= 0) {
                    auto bdiff = model::diff::block_diff_ptr_t{};
                    bdiff = new model::diff::modify::blocks_availability_t(*info.get_source(), valid_blocks);
                    send<model::payload::block_update_t>(coordinator, std::move(bdiff), this);
                }
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::lock_file_t(*info.get_file(), false);
                send<model::payload::model_update_t>(coordinator, std::move(diff), this);
            }
        }
    }

    if (!info.is_valid() && info.get_queue_size() == 0) {
        auto &file = *info.get_file();
        auto diff = model::diff::cluster_diff_ptr_t{};
        diff = new model::diff::modify::lock_file_t(file, false);
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);

        LOG_DEBUG(log, "{}, removing temporal of '{}' as it corrupted", identity, file.get_full_name());
        auto r = info.remove();
        if (r) {
            model::io_errors_t errors{model::io_error_t{info.get_path(), r.assume_error()}};
            send<model::payload::io_error_t>(coordinator, std::move(errors));
        }
    }

    if (!queued_next) {
        send<payload::scan_progress_t>(address, info.get_task());
    }
}

void scan_actor_t::commit_new_file(new_chunk_iterator_t &info) noexcept {
    assert(info.is_complete());
    auto &hashes = info.get_hashes();
    auto file = proto::FileInfo();
    file.set_name(info.get_path().string());
    file.set_type(proto::FileInfoType::FILE);
    file.set_size(info.get_size());
    file.set_block_size(info.get_block_size());
    int offset = 0;

    for (auto &b : hashes) {
        auto block = file.add_blocks();
        block->set_hash(b.digest);
        block->set_weak_hash(b.weak);
        block->set_size(b.size);
        block->set_offset(offset);
        offset += b.size;
    }

    auto diff = model::diff::cluster_diff_ptr_t{};
    auto folder_id = std::string(info.get_task()->get_folder_id());
    diff = new model::diff::modify::new_file_t(*cluster, std::move(folder_id), file);
    send<model::payload::model_update_t>(coordinator, std::move(diff), this);
}

void scan_actor_t::on_hash_new(hasher::message::digest_response_t &res) noexcept {
    --requested_hashes;

    auto &rp = res.payload.req->payload.request_payload;
    auto msg = static_cast<message::hash_anew_t *>(rp.custom.get());
    auto &info = msg->payload;

    auto &path = info.get_path();
    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        LOG_ERROR(log, "{}, on_hash_new, file: {}, block = {}, error: {}", identity, path.string(), rp.block_index,
                  ee->message());
        return do_shutdown(ee);
    }

    auto &hash_info = res.payload.res;

    info.ack(rp.block_index, hash_info.weak, hash_info.digest);

    bool queued_next = false;
    while (info.has_more_chunks()) {
        hash_next(*msg, address);
        bool can_process_more = requested_hashes < requested_hashes_limit;
        queued_next = !can_process_more;
        if (info.is_complete()) {
            commit_new_file(info);
        }
    }
#if 0
    if (!info.is_valid() && info.get_queue_size() == 0) {
        auto &file = *info.get_file();
        auto diff = model::diff::cluster_diff_ptr_t{};
        diff = new model::diff::modify::lock_file_t(file, false);
        send<model::payload::model_update_t>(coordinator, std::move(diff), this);

        LOG_DEBUG(log, "{}, removing temporal of '{}' as it corrupted", identity, file.get_full_name());
        auto r = info.remove();
        if (r) {
            model::io_errors_t errors{model::io_error_t{info.get_path(), r.assume_error()}};
            send<model::payload::io_error_t>(coordinator, std::move(errors));
        }
    }

    if (!queued_next) {
        send<payload::scan_progress_t>(address, info.get_task());
    }
#endif
}
