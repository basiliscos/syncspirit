// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/modify/suspend_folder.h"
#include "presentation/folder_entity.h"
#include "fs/fs_slave.h"
#include "names.h"
#include "presentation/presence.h"

using namespace syncspirit;
using namespace syncspirit::net;

namespace bfs = std::filesystem;

namespace {

struct folder_context_t : boost::intrusive_ref_counter<folder_context_t, boost::thread_safe_counter> {
    folder_context_t(model::folder_info_ptr_t local_folder_) noexcept : local_folder{local_folder_} {}
    model::folder_info_ptr_t local_folder;
};

using folder_context_ptr_t = r::intrusive_ptr_t<folder_context_t>;

struct folder_slave_t final : fs::fs_slave_t {
    using local_keeper_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    enum class state_t { folder_scan, dir_scan, done };

    folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept
        : context{context_}, state{state_t::folder_scan}, actor{std::move(actor_)} {
        log = actor->log;
        auto path = context->local_folder->get_folder()->get_path();
        push(fs::task::scan_dir_t(std::move(path)));
    }

    bool post_process() noexcept {
        auto folder_id = context->local_folder->get_folder()->get_id();
        LOG_TRACE(log, "postpocess of '{}', state = {}", folder_id, static_cast<int>(state));
        bool done = tasks_out.empty();
        if (!done) {
            if (state == state_t::folder_scan) {
                post_process_folder();
            } else if (state == state_t::dir_scan) {
                auto &task = tasks_out.front();
                auto &t = std::get<fs::task::scan_dir_t>(task);
                auto &ec = t.ec;
                assert(!ec);
                process_dir(t);
                tasks_out.pop_front();
            }
        }
        if (done) {
            state = state_t::done;
            using clock_t = r::pt::microsec_clock;
            auto now = clock_t::local_time();
            auto diff = model::diff::cluster_diff_ptr_t{};
            diff = new model::diff::local::scan_finish_t(folder_id, now);
            actor->send<model::payload::model_update_t>(actor->coordinator, std::move(diff));
            done = true;
        } else {
            if (!pending_disk_scans.empty()) {
                auto first = pending_disk_scans.front();
                push(std::move(first));
                pending_disk_scans.pop_front();
            }
        }
        return done;
    }

    void post_process_folder() noexcept {
        assert(tasks_out.size() == 1);
        auto &task = tasks_out.front();
        auto &t = std::get<fs::task::scan_dir_t>(task);
        auto &ec = t.ec;
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        if (ec) {
            LOG_TRACE(log, "suspending {}, due to: {}", folder_id, ec.message());
            auto diff = model::diff::cluster_diff_ptr_t{};
            diff = new model::diff::modify::suspend_folder_t(*folder, true, ec);
            actor->send<model::payload::model_update_t>(actor->coordinator, std::move(diff));
        } else {
            if (folder->is_suspended()) {
                LOG_TRACE(log, "un-suspending {}", folder_id);
                auto diff = model::diff::cluster_diff_ptr_t{};
                diff = new model::diff::modify::suspend_folder_t(*folder, false);
                actor->send<model::payload::model_update_t>(actor->coordinator, std::move(diff));
            } else {
                process_dir(t);
                if (!pending_disk_scans.empty()) {
                    state = state_t::dir_scan;
                }
            }
        }
        tasks_out.pop_front();
    }

    void process_dir(fs::task::scan_dir_t &dir) noexcept {
        auto folder = context->local_folder->get_folder();
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        auto local_device = folder->get_cluster()->get_device();
        auto folder_presence = folder_entity->get_presence(local_device.get());
        auto &model_children = folder_presence->get_children();
        auto it_model = model_children.begin();
        auto it_disk = dir.child_infos.begin();

        while (it_disk != dir.child_infos.end()) {
            auto &info = *it_disk;
            if (info.ec) {
                std::abort();
            } else if (info.status.type() == bfs::file_type::directory) {
                auto task = fs::task::scan_dir_t(std::move(dir.path / info.path));
                pending_disk_scans.emplace_back(std::move(task));
            }
            ++it_disk;
        }
    }

    tasks_t pending_disk_scans;
    folder_context_ptr_t context;
    local_keeper_ptr_t actor;
    utils::logger_t log;
    state_t state;
};

} // namespace

local_keeper_t::local_keeper_t(config_t &config)
    : r::actor_base_t(config), sequencer{std::move(config.sequencer)}, cluster{config.cluster} {
    assert(cluster);
    assert(sequencer);
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
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) { p.subscribe_actor(&local_keeper_t::on_post_process); });
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

auto local_keeper_t::operator()(const model::diff::local::scan_start_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    bool do_scan = true;
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if (folder->is_suspended() && !folder->get_suspend_reason()) {
        do_scan = false;
    }
    if (do_scan) {
        LOG_DEBUG(log, "initiating scan of {}", diff.folder_id);
        auto local_folder = folder->get_folder_infos().by_device(*cluster->get_device());
        auto ctx = folder_context_ptr_t(new folder_context_t(local_folder));
        auto slave = fs::payload::foreign_executor_prt_t();
        slave.reset(new folder_slave_t(std::move(ctx), this));
        route<fs::payload::foreign_executor_prt_t>(fs_addr, address, std::move(slave));
    } else {
        LOG_DEBUG(log, "skipping scan of {}", diff.folder_id);
    }
    return diff.visit_next(*this, custom);
}

void local_keeper_t::on_post_process(fs::message::foreign_executor_t &msg) noexcept {
    auto &slave = static_cast<folder_slave_t &>(*msg.payload.get());
    auto folder_id = slave.context->local_folder->get_folder()->get_id();
    auto done = slave.post_process();
    if (!done) {
        redirect(&msg, fs_addr, address);
    }
}
