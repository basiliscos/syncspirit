// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "model/diff/advance/advance.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/blocks_availability.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/file_availability.h"
#include "model/diff/modify/suspend_folder.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/misc/resolver.h"
#include "presentation/folder_entity.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "names.h"
#include "presentation/folder_presence.h"
#include "presentation/local_file_presence.h"
#include "presentation/presence.h"
#include "presentation/cluster_file_presence.h"
#include "proto/proto-helpers.h"
#include "local_keeper/folder_slave.h"
#include "utils/platform.h"

#include <algorithm>
#include <memory_resource>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::net;
using namespace syncspirit::net::local_keeper;

namespace bfs = std::filesystem;
namespace sys = boost::system;

using boost::nowide::narrow;

local_keeper_t::local_keeper_t(config_t &config)
    : r::actor_base_t(config), sequencer{std::move(config.sequencer)},
      concurrent_hashes_left{static_cast<std::int32_t>(config.concurrent_hashes)},
      concurrent_hashes_limit{concurrent_hashes_left}, files_scan_iteration_limit{config.files_scan_iteration_limit} {
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
        p.discover_name(names::fs_actor, fs_addr, false).link(false);
        p.discover_name(net::names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&local_keeper_t::on_model_update, coordinator);
                plugin->subscribe_actor(&local_keeper_t::on_thread_ready, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&local_keeper_t::on_post_process);
        p.subscribe_actor(&local_keeper_t::on_digest);
        p.subscribe_actor(&local_keeper_t::on_create_dir);
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

auto local_keeper_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool do_scan = true;
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if ((folder->is_suspended() && !folder->get_suspend_reason()) || state != r::state_t::OPERATIONAL) {
        do_scan = false;
    }
    if (do_scan) {
        LOG_DEBUG(log, "initiating scan of {}", diff.folder_id);
        auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
        auto ctx = folder_context_ptr_t(new folder_context_t(local_folder));
        auto backend = new folder_slave_t(std::move(ctx), this);
        auto stack_ctx = stack_context_t(concurrent_hashes_left);
        backend->process_stack(stack_ctx);
        backend->prepare_task();
        auto slave = fs::payload::foreign_executor_prt_t(backend);
        route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(slave));
        ++fs_tasks;
    } else {
        LOG_DEBUG(log, "skipping scan of {}", diff.folder_id);
    }
    return diff.visit_next(*this, custom);
}

auto local_keeper_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder_id = db::get_id(diff.db);
    auto folder = cluster->get_folders().by_id(folder_id);
    auto path = fs::payload::create_dir_t(folder->get_path(), folder_id);
    route<fs::payload::create_dir_t>(fs_addr, address, std::move((path)));
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
        }
    }
}

void local_keeper_t::on_post_process(fs::message::foreign_executor_t &msg) noexcept {
    --fs_tasks;
    LOG_TRACE(log, "on_post_process, active tasks: {}", fs_tasks);
    assert(fs_tasks >= 0);
    if (state == r::state_t::OPERATIONAL) {
        auto &slave = static_cast<folder_slave_t &>(*msg.payload.get());
        auto folder_id = slave.context->local_folder->get_folder()->get_id();
        auto has_pending = slave.post_process();
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
        auto &slave = *hash_ctx.slave.get();
        auto has_pending = slave.post_process(*hash_ctx.hash_file.get(), msg);
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
