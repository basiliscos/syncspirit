// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "local_keeper.h"
#include "names.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/suspend_folder.h"
#include "model/diff/modify/upsert_folder.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "local_keeper/hash_context.h"
#include "local_keeper/folder_context.h"
#include "local_keeper/folder_slave.h"
#include "presentation/folder_entity.h"
#include "presentation/folder_presence.h"
#include "utils/string_comparator.hpp"

#include <boost/nowide/convert.hpp>
#include <memory_resource>
#include <iterator>

using namespace syncspirit;
using namespace syncspirit::net;
using namespace syncspirit::net::local_keeper;

namespace bfs = std::filesystem;
namespace sys = boost::system;

using boost::nowide::narrow;
using boost::nowide::widen;
using UT = fs::update_type_t;

struct local_keeper_t::lc_context_t final : local_keeper::stack_context_t {
    using parent_t = local_keeper::stack_context_t;
    using folder_contexts_t = local_keeper::folder_slave_t::folder_contexts_t;
    lc_context_t(local_keeper_t *k, folder_slave_t *slave) noexcept
        : parent_t(*k->cluster, *k->sequencer, k->concurrent_hashes_left, k->concurrent_hashes_limit,
                   k->files_scan_iteration_limit, k->watcher_impl),
          actor(k) {
        if (slave && !actor->delayed.empty()) {
            slave->push(std::move(actor->delayed));
        }
    }

    ~lc_context_t() {
        if (new_contexts.size()) {
            if (!actor->fs_tasks) {
                auto backend = new folder_slave_t();
                auto backend_keeper = fs::payload::foreign_executor_prt_t(backend);
                backend->push(std::move(new_contexts));
                backend->process_stack(*this);
                backend->prepare_task();
                auto &fs_addr = actor->fs_addr;
                auto &back_addr = actor->address;
                actor->route<fs::payload::foreign_executor_prt_t>(fs_addr, back_addr, std::move(backend_keeper));
                ++actor->fs_tasks;
            } else {
                auto inserter = std::back_insert_iterator(actor->delayed);
                std::move(new_contexts.begin(), new_contexts.end(), inserter);
            }
        }
        if (diff) {
            actor->send<model::payload::model_update_t>(actor->coordinator, std::move(diff));
        }
        actor->concurrent_hashes_left = hashes_pool;
    }

    bool has_in_progress_io() const noexcept override { return actor->fs_tasks != 0; }

    virtual rotor::address_ptr_t get_back_address() const noexcept override { return actor->get_address(); }

    local_keeper_t *actor;
    folder_contexts_t new_contexts;
};

local_keeper_t::local_keeper_t(config_t &config)
    : r::actor_base_t(config), sequencer{std::move(config.sequencer)},
      concurrent_hashes_left{static_cast<std::int32_t>(config.concurrent_hashes)},
      concurrent_hashes_limit{concurrent_hashes_left}, files_scan_iteration_limit{config.files_scan_iteration_limit},
      watcher_impl{config.watcher_impl} {
    assert(sequencer);
    assert(concurrent_hashes_left);
    assert(files_scan_iteration_limit);
}

void local_keeper_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.local_keeper", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::fs_actor, fs_addr, true).link(false);
        if (watcher_impl != syncspirit_watcher_impl_t::none) {
            p.discover_name(names::watcher, watcher_addr, true).link(false);
        }
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&local_keeper_t::on_change, coordinator);
                plugin->subscribe_actor(&local_keeper_t::on_model_update, coordinator);
                plugin->subscribe_actor(&local_keeper_t::on_thread_ready, coordinator);
                plugin->subscribe_actor(&local_keeper_t::on_app_ready, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&local_keeper_t::on_post_process);
        p.subscribe_actor(&local_keeper_t::on_digest);
        p.subscribe_actor(&local_keeper_t::on_create_dir);
        p.subscribe_actor(&local_keeper_t::on_watch_dir);
        p.subscribe_actor(&local_keeper_t::on_unwatch_dir);
    });
}

void local_keeper_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    r::actor_base_t::on_start();
}

void local_keeper_t::shutdown_start() noexcept {
    LOG_TRACE(log, "shutdown_start");
    r::actor_base_t::shutdown_start();
}

void local_keeper_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *msg.payload.diff;
    auto r = diff.visit(*this, const_cast<void *>(msg.payload.custom));
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}

void local_keeper_t::on_thread_ready(model::message::thread_ready_t &message) noexcept {
    auto &p = message.payload;
    if (p.thread_id == std::this_thread::get_id()) {
        LOG_TRACE(log, "on_thread_ready");
        cluster = message.payload.cluster;
    }
}

void local_keeper_t::on_app_ready(model::message::app_ready_t &) noexcept {
    if (watcher_impl != syncspirit_watcher_impl_t::none) {
        for (auto &[folder, _] : cluster->get_folders()) {
            auto &f = *folder;
            if (f.is_watched()) {
                route<fs::payload::watch_folder_t>(watcher_addr, address, f.get_path(), f.get_id());
            }
        }
    }
}

auto local_keeper_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool do_scan = true;
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if ((folder->is_suspended() && !folder->get_suspend_reason()) || state != r::state_t::OPERATIONAL) {
        do_scan = false;
    }
    if (do_scan) {
        LOG_DEBUG(log, "initiating scan of {} from '{}'", diff.folder_id, diff.sub_dir);
        auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
        auto opt = local_keeper::make_context(local_folder, diff.sub_dir);
        if (!opt) {
            auto &ec = opt.assume_error();
            LOG_ERROR(log, "cannot initialize backend: {}", ec.message());
            auto now = r::pt::microsec_clock::local_time();
            auto finish = model::diff::cluster_diff_ptr_t();
            finish = new model::diff::local::scan_finish_t(diff.folder_id, now);
            send<model::payload::model_update_t>(coordinator, std::move(finish));
        } else {
            lc_context_t(this, {}).new_contexts.emplace_back(std::move(opt.value()));
        }
    } else {
        LOG_DEBUG(log, "skipping scan of {}", diff.folder_id);
    }
    return diff.visit_next(*this, custom);
}

auto local_keeper_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder_id = db::get_id(diff.db);
    auto folder = cluster->get_folders().by_id(folder_id);
    if (diff.is_new) {
        auto create_dir = fs::payload::create_dir_t(folder->get_path(), folder_id);
        route<fs::payload::create_dir_t>(fs_addr, address, std::move((create_dir)));
    } else {
        if (watcher_impl != syncspirit_watcher_impl_t::none) {
            auto count = watched_folders.count(folder_id);
            if (folder->is_watched()) {
                if (!count) {
                    route<fs::payload::watch_folder_t>(watcher_addr, address, folder->get_path(), folder_id);
                }
            } else {
                if (count) {
                    route<fs::payload::unwatch_folder_t>(watcher_addr, address, std::string(folder_id));
                }
            }
        }
    }
    return diff.visit_next(*this, custom);
}

auto local_keeper_t::operator()(const model::diff::modify::remove_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &folder_id = diff.folder_id;
    if (watcher_impl != syncspirit_watcher_impl_t::none) {
        auto count = watched_folders.count(folder_id);
        if (count) {
            route<fs::payload::unwatch_folder_t>(watcher_addr, address, std::string(folder_id));
        }
    }
    return diff.visit_next(*this, custom);
}

void local_keeper_t::on_create_dir(fs::message::create_dir_t &message) noexcept {
    auto &p = message.payload;
    auto &ec = message.payload.ec;
    auto folder = cluster->get_folders().by_id(p.folder_id);
    if (folder) {
        LOG_TRACE(log, "on_create_dir, folder path: {}", narrow(p.generic_wstring()));
        if (ec) {
            LOG_WARN(log, "on_create_dir, cannot create path '{}': {}, suspending", narrow(p.generic_wstring()),
                     ec.message());
            auto diff = model::diff::cluster_diff_ptr_t();
            diff = new model::diff::modify::suspend_folder_t(*folder, true, ec);
            send<model::payload::model_update_t>(coordinator, std::move(diff));
        } else {
            if (folder->is_watched() && watcher_impl != syncspirit_watcher_impl_t::none) {
                route<fs::payload::watch_folder_t>(watcher_addr, address, folder->get_path(), p.folder_id);
            }
        }
    }
}

void local_keeper_t::on_post_process(fs::message::foreign_executor_t &msg) noexcept {
    --fs_tasks;
    LOG_TRACE(log, "on_post_process, active tasks: {}", fs_tasks);
    assert(fs_tasks >= 0);
    if (state == r::state_t::OPERATIONAL) {
        auto &slave = static_cast<folder_slave_t &>(*msg.payload.get());
        auto stack_ctx = lc_context_t(this, &slave);
        auto has_pending = slave.post_process(stack_ctx);
        if (has_pending && fs_tasks == 0) {
            if (slave.ec) {
                LOG_ERROR(log, "cannot process folder any longer: {}", slave.ec.message());
            } else {
                slave.prepare_task();
                slave.ec = utils::make_error_code(utils::error_code_t::no_action);
                redirect(&msg, fs_addr, address);
                LOG_TRACE(log, "redirected {}", (void *)&slave);
                ++fs_tasks;
            }
        }
    } else {
        LOG_DEBUG(log, "skipping post-processing of foreign executor (non-operational)");
    }
}

void local_keeper_t::on_digest(hasher::message::digest_t &msg) noexcept {
    auto &p = msg.payload;
    LOG_TRACE(log, "on_digest, block size: {}, index: {}", p.data.size(), p.block_index);
    if (state == r::state_t::OPERATIONAL) {
        auto hash_ctx = *static_cast<hash_context_t *>(p.context.get());
        auto &hash_file = *hash_ctx.hash_file.get();
        auto &slave = *hash_ctx.slave.get();
        auto folder_ctx = hash_ctx.folder_context.get();
        auto stack_ctx = lc_context_t(this, &slave);
        auto has_pending = slave.post_process(hash_file, folder_ctx, msg, stack_ctx);
        if (has_pending && fs_tasks == 0) {
            slave.prepare_task();
            LOG_TRACE(log, "routed {}", (void *)&slave);
            route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(hash_ctx.slave));
            ++fs_tasks;
        }
    } else {
        LOG_DEBUG(log, "skipping post-processing of hashed digest (non-operational)");
    }
}

void local_keeper_t::on_watch_dir(fs::message::watch_folder_t &message) noexcept {
    auto &p = message.payload;
    auto &ec = p.ec;
    if (ec) {
        LOG_WARN(log, "cannot watch folder '{}': {}", p.folder_id, ec.message());
    } else {
        LOG_DEBUG(log, "watching fodler '{}'", p.folder_id);
        watched_folders.emplace(std::string(p.folder_id));
    }
}

void local_keeper_t::on_unwatch_dir(fs::message::unwatch_folder_t &message) noexcept {
    auto &folder_id = message.payload.folder_id;
    LOG_DEBUG(log, "on_unwatch_dir: {}", folder_id);
    auto it = watched_folders.find(folder_id);
    if (it != watched_folders.end()) {
        watched_folders.erase(it);
    }
}

void local_keeper_t::on_change(fs::message::folder_changes_t &message) noexcept {
    auto &p = message.payload;
    LOG_DEBUG(log, "on_change, affects {} folder(s)", p.size());
    auto &folders_map = cluster->get_folders();
    auto stack_ctx = lc_context_t(this, {});
    for (auto &folder_change : p) {
        auto &folder_id = folder_change.folder_id;
        auto folder = folders_map.by_id(folder_id);
        if (!folder) {
            LOG_DEBUG(log, "there is no folder '{}' in the model", folder_id);
        } else {
            auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
            on_changes(*local_folder, folder_change.file_changes, stack_ctx);
        }
    }
}

void local_keeper_t::handle_rename(fs::payload::file_info_t &change, const model::folder_info_t &local_folder,
                                   lc_context_t &stack_ctx) noexcept {
    using namespace model::diff;

    auto folder_id = local_folder.get_folder()->get_id();
    auto new_name = proto::get_name(change);
    auto &prev_name = change.prev_path;
    LOG_DEBUG(log, "handle rename '{}' -> '{}' in folder '{}'", prev_name, new_name, folder_id);
    auto &local_files = local_folder.get_file_infos();
    auto f_prev = local_files.by_name(prev_name);
    if (!f_prev) {
        LOG_WARN(log, "no '{}' in local folder", prev_name);
        return;
    }

    auto pr_new = f_prev->as_proto(true);
    auto pr_prev = f_prev->as_proto(false);

    proto::set_name(pr_new, new_name);
    proto::set_deleted(pr_prev, true);
    stack_ctx.push(new advance::local_update_t(*cluster, *sequencer, std::move(pr_new), folder_id));
    stack_ctx.push(new advance::local_update_t(*cluster, *sequencer, std::move(pr_prev), folder_id, true));
}

void local_keeper_t::on_changes(model::folder_info_t &local_folder, fs::payload::file_changes_t &changes,
                                lc_context_t &stack_ctx) noexcept {
    using namespace model::diff;
    using scheduled_dirs_t = std::pmr::unordered_set<std::pmr::string, utils::string_hash_t, utils::string_eq_t>;

    auto buffer = std::array<std::byte, 1024 * 8>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto scheduled_dirs = scheduled_dirs_t(allocator);

    auto folder = local_folder.get_folder();
    auto folder_id = folder->get_id();
    auto &files_map = local_folder.get_file_infos();
    auto unexamined = local_keeper::unexamined_items_t();
    auto augmentation = local_folder.get_augmentation().get();
    auto folder_presence = static_cast<presentation::folder_presence_t *>(augmentation);

    auto immediate_update = [&](fs::payload::file_info_t &change) {
        auto name = proto::get_name(change);
        auto file = files_map.by_name(name);
        bool update = true;
        if (file) {
            update = !file->identical_by_content_to(change);
        }
        if (proto::get_type(change) == proto::FileInfoType::DIRECTORY) {
            auto n = std::pmr::string(name, allocator);
            scheduled_dirs.emplace(n);
        }
        if (update) {
            auto renamed = (change.update_reason == UT::meta) && !change.prev_path.empty();
            if (renamed) {
                handle_rename(change, local_folder, stack_ctx);
            } else {
                stack_ctx.push(new advance::local_update_t(*cluster, *sequencer, std::move(change), folder_id));
            }
        } else {
            LOG_DEBUG(log, "ignoring update on '{}'", name);
        }
    };
    auto delayed_update = [&](fs::payload::file_info_t change) {
        using CI = local_keeper::child_info_t;
        auto name = proto::get_name(change);
        auto is_dir = proto::get_type(change) == proto::FileInfoType::DIRECTORY;
        auto relation = folder_presence->get_link(name, is_dir);
        auto has_parent = relation.parent;
        auto inside_subdir = name.find_last_of('/');
        auto subdir = std::string_view{};
        if (inside_subdir != std::string::npos) {
            subdir = name.substr(0, inside_subdir);
        }
        if (!has_parent && !subdir.empty() && !scheduled_dirs.count(subdir)) {
            LOG_WARN(log, "no parent for '{}' in folder '{}', ignoring orphan", name, folder_id);
            return;
        }
        if (watcher_impl == syncspirit_watcher_impl_t::inotify && !subdir.empty() && scheduled_dirs.count(subdir)) {
            LOG_DEBUG(log, "ignoring '{}' in folder '{}', parent dir scan is scheduled", name, folder_id);
            return;
        }
        auto path = folder->get_path() / widen(name);
        auto child_info = CI(std::move(change), std::move(path), relation.child, relation.parent);
        unexamined.emplace_back(std::move(child_info));
    };
    for (auto &change : changes) {
        switch (change.update_reason) {
        case UT::created: {
            if (proto::get_size(change) == 0) {
                auto schedule_scan = (watcher_impl == syncspirit_watcher_impl_t::inotify) &&
                                     proto::get_type(change) == proto::FileInfoType::DIRECTORY;
                if (schedule_scan) {
                    delayed_update(change);
                } else {
                    immediate_update(change);
                }
            } else {
                delayed_update(std::move(change));
            }
            break;
        }
        case UT::meta: {
            immediate_update(change);
            break;
        }
        case UT::deleted: {
            immediate_update(change);
            break;
        }
        case UT::content: {
            delayed_update(std::move(change));
            break;
        }
        default:
            LOG_WARN(log, "not implemented");
        }
    }
    if (!unexamined.empty()) {
        auto folder_ctx = local_keeper::make_context(&local_folder, std::move(unexamined));
        stack_ctx.new_contexts.emplace_back(std::move(folder_ctx));
    }
}
