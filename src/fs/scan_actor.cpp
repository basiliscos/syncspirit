// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "scan_actor.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/modify/lock_file.h"
#include "model/diff/local/blocks_availability.h"
#include "model/diff/local/io_failure.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_start.h"
#include "presentation/cluster_file_presence.h"
#include "presentation/folder_entity.h"
#include "presentation/presence.h"
#include "net/names.h"

#include <algorithm>
#include <memory_resource>
#include <list>
#include <iterator>

namespace sys = boost::system;
using namespace syncspirit::fs;

template <class> inline constexpr bool always_false_v = false;

scan_actor_t::scan_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, cluster{cfg.cluster}, sequencer{cfg.sequencer}, fs_config{cfg.fs_config},
      rw_cache(cfg.rw_cache), requested_hashes_limit{cfg.requested_hashes_limit} {
    assert(sequencer);
    assert(rw_cache);
}

void scan_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(net::names::fs_scanner, false);
        log = utils::get_logger(identity);
        new_files = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(net::names::fs_scanner, address);
        p.discover_name(net::names::hasher_proxy, hasher_proxy, true).link();
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&scan_actor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&scan_actor_t::on_scan);
        p.subscribe_actor(&scan_actor_t::on_hash);
        p.subscribe_actor(&scan_actor_t::on_hash_new, new_files);
        p.subscribe_actor(&scan_actor_t::on_rehash);
        p.subscribe_actor(&scan_actor_t::on_hash_anew);
    });
}

void scan_actor_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void scan_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    get_supervisor().shutdown();
    r::actor_base_t::shutdown_finish();
}

auto scan_actor_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    LOG_DEBUG(log, "initiating scan of {}", diff.folder_id);
    auto task = scan_task_ptr_t(new scan_task_t(cluster, diff.folder_id, rw_cache, fs_config));
    send<payload::scan_progress_t>(address, std::move(task));
    return diff.visit_next(*this, custom);
}

void scan_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.visit(*this, nullptr);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void scan_actor_t::on_scan(message::scan_progress_t &message) noexcept {
    auto &task = message.payload.task;
    auto guard = task->guard(*this, coordinator);
    auto folder_id = task->get_folder_id();
    LOG_TRACE(log, "on_scan, folder = {}", folder_id);

    auto folder = cluster->get_folders().by_id(folder_id);
    bool stop_processing = false;
    bool completed = false;
    if (folder->is_synchronizing() || folder->is_suspended()) {
        LOG_DEBUG(log, "cancelling folder '{}' scanning", folder_id);
        stop_processing = true;
        completed = true;
    } else {
        auto r = task->advance();
        std::visit(
            [&](auto &&r) {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, bool>) {
                    stop_processing = !r;
                    if (stop_processing) {
                        completed = true;
                    }
                } else if constexpr (std::is_same_v<T, scan_errors_t>) {
                    task->push(new model::diff::local::io_failure_t(std::move(r)));
                } else if constexpr (std::is_same_v<T, unchanged_meta_t>) {
                    task->push(new model::diff::local::file_availability_t(r.file));
                } else if constexpr (std::is_same_v<T, removed_t>) {
                    auto &file = *r.file;
                    LOG_DEBUG(log, "locally removed {}", file.get_full_name());
                    auto folder = file.get_folder_info()->get_folder();
                    auto folder_id = std::string(folder->get_id());
                    auto fi = file.as_proto(false);
                    proto::set_deleted(fi, true);
                    task->push(new model::diff::advance::local_update_t(*cluster, *sequencer, std::move(fi),
                                                                        std::move(folder_id)));
                } else if constexpr (std::is_same_v<T, changed_meta_t>) {
                    auto &file = *r.file;
                    auto &metadata = r.metadata;
                    auto errs = initiate_hash(task, file.get_path(), metadata);
                    if (errs.empty()) {
                        stop_processing = true;
                    } else {
                        task->push(new model::diff::local::io_failure_t(std::move(errs)));
                    }
                } else if constexpr (std::is_same_v<T, unknown_file_t>) {
                    auto errs = initiate_hash(task, r.path, r.metadata);
                    if (errs.empty()) {
                        stop_processing = true;
                    } else {
                        task->push(new model::diff::local::io_failure_t(std::move(errs)));
                    }
                } else if constexpr (std::is_same_v<T, incomplete_t>) {
                    auto &f = r.file;
                    stop_processing = true;
                    send<payload::rehash_needed_t>(address, task, std::move(f), std::move(r.opened_file));
                } else if constexpr (std::is_same_v<T, incomplete_removed_t>) {
                    auto &file = *r.file;
                    if (file.is_locked()) {
                        task->push(new model::diff::modify::lock_file_t(file, false));
                    }
                } else if constexpr (std::is_same_v<T, orphaned_removed_t>) {
                    // NO-OP
                } else if constexpr (std::is_same_v<T, file_error_t>) {
#if 0
                    auto diff = model::diff::cluster_diff_ptr_t{};
                    diff = new model::diff::modify::lock_file_t(*r.file, false);
                    send<model::payload::model_update_t>(coordinator, /std::move(diff), this);
                    auto path = make_temporal(r.file->get_path());
                    scan_errors_t errors{scan_error_t{path, r.ec}};
                    send<model::payload::io_error_t>(coordinator, std::move(errors));
#endif
                    std::abort();
                } else {
                    static_assert(always_false_v<T>, "non-exhaustive visitor!");
                }
            },
            r);
    }

    if (!stop_processing) {
        guard.send_progress();
    } else if (completed) {
        LOG_DEBUG(log, "completed scanning of {}", folder_id);
        post_scan(*task);
        auto now = clock_t::local_time();
        task->push(new model::diff::local::scan_finish_t(folder_id, now));
        guard.send_by_force();
    }
}

auto scan_actor_t::initiate_hash(scan_task_ptr_t task, const bfs::path &path, proto::FileInfo &metadata) noexcept
    -> scan_errors_t {
    file_ptr_t file;
    LOG_DEBUG(log, "will try to initiate hashing of {}", path.string());
    if (proto::get_type(metadata) == proto::FileInfoType::FILE) {
        auto opt = file_t::open_read(path);
        if (!opt) {
            auto &ec = opt.assume_error();
            LOG_WARN(log, "error opening file {}: {}", path.string(), ec.message());
            return scan_errors_t{scan_error_t{path, ec}};
        }
        file = new file_t(std::move(opt.value()));
    }
    send<payload::hash_anew_t>(address, std::move(task), std::move(metadata), std::move(file));
    return {};
}

void scan_actor_t::on_rehash(message::rehash_needed_t &message) noexcept {
    LOG_TRACE(log, "on_rehash");
    auto initial = requested_hashes;
    hash_next(message, address);
    if (initial == requested_hashes) {
        send<payload::scan_progress_t>(address, message.payload.get_task());
    }
}

void scan_actor_t::on_hash_anew(message::hash_anew_t &message) noexcept {
    LOG_TRACE(log, "on_hash_anew");
    auto initial = requested_hashes;
    hash_next(message, new_files);
    // file w/o context
    if (initial == requested_hashes) {
        auto &p = message.payload;
        commit_new_file(p);
        send<payload::scan_progress_t>(address, p.get_task());
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
                auto &task = *info.get_task().get();
                auto guard = task.guard(*this, coordinator);
                auto err = scan_error_t{info.get_path(), opt.assume_error()};
                task.push(new model::diff::local::io_failure_t(std::move(err)));
                return;
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

    auto file = info.get_file();
    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        LOG_ERROR(log, "on_hash, file: {}, block = {}, error: {}", file->get_full_name(), rp.block_index,
                  ee->message());
        return do_shutdown(ee);
    }

    bool queued_next = false;
    auto &digest = res.payload.res.digest;
    auto block_index = rp.block_index;
    info.ack_block(digest, block_index);
    hash_next(*msg, address);
    bool can_process_more = requested_hashes < requested_hashes_limit;
    auto &task = *info.get_task().get();
    auto guard = task.guard(*this, coordinator);
    queued_next = !can_process_more;
    if (info.is_complete()) {
        if (info.has_valid_blocks()) {
            if (!file->is_locally_available()) {
                task.push(new model::diff::local::blocks_availability_t(*file, info.valid_blocks()));
            } else {
                LOG_DEBUG(log, "file '{}' is yet to be flushed, ignoring it", file->get_name());
            }
        } else {
            auto &file = *info.get_file();
            LOG_DEBUG(log, "removing temporal of '{}' as it corrupted", file.get_full_name());
            auto r = info.remove();
            if (r) {
                auto err = scan_error_t{info.get_path(), r.assume_error()};
                task.push(new model::diff::local::io_failure_t(std::move(err)));
            }
        }
    }

    if (!queued_next && !requested_hashes) {
        send<payload::scan_progress_t>(address, info.get_task());
    }
}

void scan_actor_t::commit_new_file(new_chunk_iterator_t &info) noexcept {
    assert(info.is_complete());
    auto &hashes = info.get_hashes();
    auto folder_id = std::string(info.get_task()->get_folder_id());
    auto &file = info.get_metadata();
    proto::set_block_size(file, info.get_block_size());
    int offset = 0;

    // file.clear_blocks();
    for (auto &b : hashes) {
        auto block_info = proto::BlockInfo{offset, b.size, b.digest, b.weak};
        proto::add_blocks(file, std::move(block_info));
        offset += b.size;
    }

    auto &task = *info.get_task().get();
    auto guard = task.guard(*this, coordinator);
    task.push(new model::diff::advance::local_update_t(*cluster, *sequencer, std::move(file), std::move(folder_id)));
}

void scan_actor_t::on_hash_new(hasher::message::digest_response_t &res) noexcept {
    --requested_hashes;

    auto &rp = res.payload.req->payload.request_payload;
    auto msg = static_cast<message::hash_anew_t *>(rp.custom.get());
    auto &info = msg->payload;

    auto &path = info.get_path();
    if (res.payload.ee) {
        auto &ee = res.payload.ee;
        LOG_ERROR(log, "on_hash_new, file: {}, block = {}, error: {}", path.string(), rp.block_index, ee->message());
        return do_shutdown(ee);
    }

    auto &hash_info = res.payload.res;
    auto block_size = rp.data.size();

    info.ack(rp.block_index, hash_info.weak, hash_info.digest, block_size);

    while (info.has_more_chunks() && (requested_hashes < requested_hashes_limit)) {
        hash_next(*msg, new_files);
    }
    if (info.is_complete()) {
        commit_new_file(info);
        send<payload::scan_progress_t>(address, info.get_task());
    }
}

void scan_actor_t::post_scan(scan_task_t &task) noexcept {
    auto folder_id = task.get_folder_id();
    auto folder = cluster->get_folders().by_id(folder_id);
    auto self_fi = folder->get_folder_infos().by_device(*cluster->get_device());
    auto &self_files = self_fi->get_file_infos();
    auto aug = folder->get_augmentation();
    auto folder_entity = dynamic_cast<presentation::folder_entity_t *>(aug.get());
    auto &seen = task.get_seen_paths();
    if (folder_entity) {
        using queue_t = std::pmr::list<presentation::entity_t *>;
        using F = syncspirit::presentation::presence_t::features_t;
        auto buffer = std::array<std::byte, 10 * 1024>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
        auto queue = queue_t(allocator);
        {
            auto &top_level_children = folder_entity->get_children();
            auto inserter = std::back_insert_iterator(queue);
            std::copy(top_level_children.begin(), top_level_children.end(), inserter);
        }
        while (!queue.empty()) {
            auto entity = queue.front();
            queue.pop_front();
            auto best = entity->get_best();
            if (best) {
                auto file_name = entity->get_path().get_full_name();
                auto it = seen.end();
                auto f = best->get_features();
                if (f & F::deleted) {
                    it = seen.find(file_name);
                    if ((it == seen.end()) && !self_files.by_name(file_name)) {
                        using local_update_t = model::diff::advance::local_update_t;
                        auto presence = static_cast<const presentation::cluster_file_presence_t *>(best);
                        auto &peer_file = presence->get_file_info();
                        auto pr_file = peer_file.as_proto(true);
                        LOG_DEBUG(log, "assuming deleted '{}' in the folder '{}'", file_name, folder_id);
                        task.push(new local_update_t(*cluster, *sequencer, std::move(pr_file), folder_id));
                    }
                }
                if (f & F::directory) {
                    if (it == seen.end()) {
                        it = seen.find(file_name);
                    }
                    auto recurse = [&]() -> bool {
                        bool creation_allowed = false;
                        if (it != seen.end()) {
                            auto &path = it->second;
                            auto ec = sys::error_code();
                            auto status = bfs::status(path, ec);
                            if (!ec) {
                                using perms_t = bfs::perms;
                                creation_allowed = (status.type() == bfs::file_type::directory) &&
                                                   ((status.permissions() & perms_t::owner_write) != perms_t::none);
                            }
                        } else {
                            creation_allowed = f & F::deleted;
                        }
                        return creation_allowed;
                    }();
                    if (recurse) {
                        auto &children = entity->get_children();
                        auto inserter = std::back_insert_iterator(queue);
                        std::copy(children.begin(), children.end(), inserter);
                    }
                }
            }
        }
    }
}
