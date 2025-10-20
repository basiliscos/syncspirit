// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_keeper.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/modify/suspend_folder.h"
#include "presentation/folder_entity.h"
#include "fs/fs_slave.h"
#include "fs/utils.h"
#include "names.h"
#include "presentation/presence.h"
#include "presentation/cluster_file_presence.h"
#include "proto/proto-helpers-bep.h"

#include <algorithm>
#include <memory_resource>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::net;

namespace bfs = std::filesystem;
namespace sys = boost::system;

namespace {

struct stack_context_t {
    model::diff::cluster_diff_ptr_t diff;
    model::diff::cluster_diff_t *next = nullptr;

    void push(model::diff::cluster_diff_t *d) noexcept {
        if (next) {
            next = next->assign_sibling(d);
        } else {
            next = d;
            diff = d;
        }
    }
};

struct folder_context_t : boost::intrusive_ref_counter<folder_context_t, boost::thread_safe_counter> {
    folder_context_t(model::folder_info_ptr_t local_folder_) noexcept : local_folder{local_folder_} {}
    model::folder_info_ptr_t local_folder;
};

using folder_context_ptr_t = r::intrusive_ptr_t<folder_context_t>;
using child_info_t = fs::task::scan_dir_t::child_info_t;
using unscanned_dir_t = bfs::path;
struct unexamined_t : child_info_t {};
struct child_ready_t : child_info_t {};
struct complete_scan_t {};
struct suspend_scan_t {
    sys::error_code ec;
};
struct unsuspend_scan_t {};

using stack_item_t =
    std::variant<unscanned_dir_t, unexamined_t, complete_scan_t, child_ready_t, suspend_scan_t, unsuspend_scan_t>;

struct folder_slave_t final : fs::fs_slave_t {
    using local_keeper_ptr_t = r::intrusive_ptr_t<net::local_keeper_t>;
    using presences_t = std::pair<presentation::presence_t *, presentation::presence_t *>;
    using stack_t = std::list<stack_item_t>;

    folder_slave_t(folder_context_ptr_t context_, local_keeper_ptr_t actor_) noexcept
        : context{context_}, actor{std::move(actor_)} {
        log = actor->log;
        auto path = context->local_folder->get_folder()->get_path();
        stack.push_front(complete_scan_t{});
        stack.push_front(unscanned_dir_t(std::move(path)));
    }

    void process_stack(stack_context_t &ctx) noexcept {
        auto try_next = true;
        while (!stack.empty() && try_next) {
            auto item = stack.front();
            stack.pop_front();
            std::visit([&](auto &item) { try_next = process(item, ctx); }, item);
        }
        if (ctx.diff) {
            actor->send<model::payload::model_update_t>(actor->coordinator, std::move(ctx.diff));
        }
    }

    void process_stack() noexcept {
        auto ctx = stack_context_t();
        process_stack(ctx);
    }

    bool process(complete_scan_t &, stack_context_t &ctx) noexcept {
        using clock_t = r::pt::microsec_clock;
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto now = clock_t::local_time();
        ctx.push(new model::diff::local::scan_finish_t(folder_id, now));
        return false;
    }

    bool process(unscanned_dir_t &path, stack_context_t &ctx) noexcept {
        auto sub_task = fs::task::scan_dir_t(std::move(path));
        pending_disk_scans.emplace_back(std::move(sub_task));
        return false;
    }

    bool process(unexamined_t &child_info, stack_context_t &ctx) noexcept {
        auto type = child_info.status.type();
        if (type == bfs::file_type::directory) {
            stack.push_front(child_ready_t(child_info));
            stack.push_front(unscanned_dir_t(child_info.path));
            return true;
        } else if (type == bfs::file_type::symlink) {
            stack.push_front(child_ready_t(child_info));
            return true;
        } else if (type == bfs::file_type::regular) {
            if (!child_info.size) {
                stack.push_front(child_ready_t(child_info));
                return true;
            }
            std::abort();
        } else {
            std::abort();
        }
    }

    bool process(suspend_scan_t &item, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &ec = item.ec;
        LOG_TRACE(log, "suspending '{}', due to: {}", folder_id, ec.message());
        ctx.push(new model::diff::modify::suspend_folder_t(*folder, true, ec));
        while (stack.size() > 1) {
            stack.pop_front();
        }
        return true;
    }

    bool process(unsuspend_scan_t &, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        LOG_TRACE(log, "un-suspending {}", folder_id);
        ctx.push(new model::diff::modify::suspend_folder_t(*folder, false));
        return true;
    }

    bool process(child_ready_t &info, stack_context_t &ctx) noexcept {
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto [parent, child] = get_presence(info.path);
        if (!child) {
            auto file = proto::FileInfo();
            auto name = fs::relativize(info.path, folder->get_path());
            auto permissions = static_cast<uint32_t>(info.status.permissions());
            auto type = [&]() -> proto::FileInfoType {
                using FT = proto::FileInfoType;
                auto t = info.status.type();
                if (t == bfs::file_type::directory)
                    return FT::DIRECTORY;
                else if (t == bfs::file_type::symlink)
                    return FT::SYMLINK;
                else
                    return FT::FILE;
            }();
            proto::set_name(file, boost::nowide::narrow(name.wstring()));
            proto::set_type(file, type);
            proto::set_modified_s(file, fs::to_unix(info.last_write_time));
            proto::set_permissions(file, permissions);
#if 0
            if (ignore_permissions == false) {
                auto permissions = static_cast<uint32_t>(status.permissions());
                proto::set_permissions(metadata, permissions);
            } else {
                proto::set_permissions(metadata, 0666);
                proto::set_no_permissions(metadata, true);
            }
#endif
            ctx.push(new model::diff::advance::local_update_t(*actor->cluster, *actor->sequencer, std::move(file),
                                                              folder_id));
        }
        return true;
    }

    bool post_process() noexcept {
        auto folder_id = context->local_folder->get_folder()->get_id();
        LOG_TRACE(log, "postpocess of '{}'", folder_id);
        auto ctx = stack_context_t();

        for (auto &t : tasks_out) {
            std::visit([&](auto &t) { post_process(t, ctx); }, t);
        }
        tasks_out.clear();

        process_stack(ctx);

        if (!pending_disk_scans.empty()) {
            auto &t = pending_disk_scans.front();
            tasks_in.emplace_back(std::move(t));
            pending_disk_scans.pop_front();
        }

        return tasks_in.empty();
    }

    presences_t get_presence(const bfs::path &path) {
        struct comparator_t {
            bool operator()(const presentation::presence_t *p, const bfs::path &name) const {
                using allocator_t = std::pmr::polymorphic_allocator<char>;

                auto buffer = std::array<std::byte, 1024>();
                auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
                auto allocator = allocator_t(&pool);
                auto own_wname = std::pmr::wstring(allocator);
                auto cp = static_cast<const presentation::cluster_file_presence_t *>(p);
                auto own_name = cp->get_file_info().get_name()->get_own_name();
                own_wname.resize(own_name.size());
                auto str = boost::nowide::widen(own_wname.data(), own_wname.size(), own_name.data());
                assert(str);
                auto wname = std::wstring_view(str, str + wcslen(str));
                return wname < name.wstring();
            }
        };

        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &root_path = folder->get_path();
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        auto local_device = folder->get_cluster()->get_device();
        auto folder_presence = folder_entity->get_presence(local_device.get());
        auto skip = std::distance(root_path.begin(), root_path.end());
        auto it = path.begin();
        std::advance(it, skip);
        auto parent = folder_presence;
        auto result = parent;
        while (it != path.end()) {
            // auto child_eq = [&]();
            auto &children = parent->get_children();
            auto it_child = std::lower_bound(children.begin(), children.end(), *it, comparator_t{});
            if (it_child != children.end()) {
                parent = result;
                result = *it_child;
            } else {
                result = {};
                break;
            }
            ++it;
        }
        return std::make_pair(parent, result);
    }

    void post_process(fs::task::scan_dir_t &task, stack_context_t &ctx) noexcept {
        auto &ec = task.ec;
        auto folder = context->local_folder->get_folder();
        auto folder_id = folder->get_id();
        auto &root_path = folder->get_path();
        bool is_root = task.path == folder->get_path();
        if (is_root) {
            if (ec) {
                stack.push_front(suspend_scan_t(ec));
                return;
            } else {
                if (folder->is_suspended()) {
                    stack.push_front(unsuspend_scan_t());
                    return;
                }
            }
        }

        auto it_disk = task.child_infos.begin();
        while (it_disk != task.child_infos.end()) {
            auto &info = *it_disk;
            if (info.ec) {
                std::abort();
            } else {
                stack.push_front(unexamined_t(info));
            }
            ++it_disk;
        }
    }

    tasks_t pending_disk_scans;
    stack_t stack;
    folder_context_ptr_t context;
    local_keeper_ptr_t actor;
    utils::logger_t log;
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
        auto backend = new folder_slave_t(std::move(ctx), this);
        backend->process_stack();
        auto slave = fs::payload::foreign_executor_prt_t(backend);
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
