// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "local_keeper.h"
#include "constants.h"
#include "fs/utils.h"
#include "local_keeper/folder_context.h"
#include "local_keeper/folder_slave.h"
#include "local_keeper/hash_context.h"
#include "model/diff/advance/local_update.h"
#include "model/diff/local/scan_finish.h"
#include "model/diff/local/scan_start.h"
#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/suspend_folder.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/messages.h"
#include "names.h"
#include "presentation/folder_entity.h"
#include "presentation/folder_presence.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "utils/string_comparator.hpp"
#include "utils/utf8.h"

#include <boost/nowide/convert.hpp>
#include <spdlog/fmt/bin_to_hex.h>
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
    using name_2_file_t =
        std::pmr::unordered_map<std::pmr::string, model::file_info_t *, utils::string_hash_t, utils::string_eq_t>;
    using file_2_name_t = std::pmr::unordered_map<model::file_info_t *, std::pmr::string>;

    lc_context_t(local_keeper_t *k, folder_slave_t *slave) noexcept
        : parent_t(*k->cluster, *k->sequencer, k->concurrent_hashes_left, k->concurrent_hashes_limit, k->watcher_impl),
          actor(k), name_2_file(allocator), file_2_name(allocator) {
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
                auto has_pending_io = backend->process_stack(*this);
                if (has_pending_io) {
                    backend->prepare_task();
                    auto &fs_addr = actor->fs_addr;
                    auto &back_addr = actor->address;
                    actor->route<fs::payload::foreign_executor_prt_t>(fs_addr, back_addr, std::move(backend_keeper));
                    ++actor->fs_tasks;
                }
            } else {
                auto inserter = std::back_insert_iterator(actor->delayed);
                std::move(new_contexts.begin(), new_contexts.end(), inserter);
            }
        }
        if (has_diffs()) {
            actor->send<model::payload::model_update_t>(actor->coordinator, consume());
        }
        actor->concurrent_hashes_left = hashes_pool;
    }

    bool has_in_progress_io() const noexcept override { return actor->fs_tasks != 0; }

    virtual rotor::address_ptr_t get_back_address() const noexcept override { return actor->get_address(); }

    local_keeper_t *actor;
    folder_contexts_t new_contexts;
    name_2_file_t name_2_file;
    file_2_name_t file_2_name;
};

local_keeper_t::local_keeper_t(config_t &config)
    : parent_t(config), sequencer{std::move(config.sequencer)},
      concurrent_hashes_left{static_cast<std::int32_t>(config.concurrent_hashes)},
      concurrent_hashes_limit{concurrent_hashes_left}, watcher_impl{config.watcher_impl} {
    assert(sequencer);
    assert(concurrent_hashes_left);
}

void local_keeper_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("net.local_keeper", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(names::fs_actor, fs_addr, true).link(false);
        if (watcher_impl != syncspirit_watcher_impl_t::none) {
            p.discover_name(names::watcher, watcher_addr, true).link(true);
        }
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&local_keeper_t::on_post_process);
        p.subscribe_actor(&local_keeper_t::on_digest);
        p.subscribe_actor(&local_keeper_t::on_create_dir);
        p.subscribe_actor(&local_keeper_t::on_watch_dir);
        p.subscribe_actor(&local_keeper_t::on_unwatch_dir);
    });
}

void local_keeper_t::post_configure_coordinator() noexcept {
    parent_t::post_configure_coordinator();
    auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
    auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
    plugin->subscribe_actor(&local_keeper_t::on_change, coordinator);
    plugin->subscribe_actor(&local_keeper_t::on_thread_ready, coordinator);
    plugin->subscribe_actor(&local_keeper_t::on_local_ready, coordinator);
}

void local_keeper_t::try_start_watching() noexcept {
    if (watcher_impl != syncspirit_watcher_impl_t::none && !started_watching) {
        if (cluster && fs_addr && watcher_addr) {
            LOG_DEBUG(log, "starting watching...");
            started_watching = true;
            for (auto &[folder, _] : cluster->get_folders()) {
                auto &f = *folder;
                if (f.is_watched()) {
                    route<fs::payload::watch_folder_t>(watcher_addr, address, f.get_path(), f.get_id());
                }
            }
        }
    }
}

void local_keeper_t::on_start() noexcept {
    parent_t::on_start();
    send<model::payload::local_up_t>(coordinator);
}

void local_keeper_t::on_local_ready(model::message::local_ready_t &) noexcept {
    LOG_TRACE(log, "on_local_ready");
    local_ready = true;
    if (fs_addr) {
        send<model::payload::sevice_lock_t>(coordinator, names::fs_actor);
    }
    if (watcher_addr) {
        send<model::payload::sevice_lock_t>(coordinator, names::watcher);
    }
    try_start_watching();
}

void local_keeper_t::shutdown_start() noexcept {
    if (local_ready) {
        if (fs_addr) {
            send<model::payload::sevice_unlock_t>(coordinator, names::fs_actor);
        }
        if (watcher_addr) {
            send<model::payload::sevice_unlock_t>(coordinator, names::watcher);
        }
    }
    parent_t::shutdown_start();
}

void local_keeper_t::visit(const model::diff::cluster_diff_t &diff, model::payload::apply_context_t &ctx) noexcept {
    LOG_TRACE(log, "visit");
    auto r = diff.visit(*this, const_cast<void *>(ctx.message_payload));
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

auto local_keeper_t::operator()(const model::diff::advance::local_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (!just_created_dirs.empty()) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto folder_path = narrow(folder->get_path().generic_wstring());
        auto name = proto::get_name(diff.proto_local);
        auto full_path = fmt::format("{}/{}", folder_path, name);
        auto it = just_created_dirs.find(full_path);
        if (it != just_created_dirs.end()) {
            just_created_dirs.erase(it);
            LOG_TRACE(log, "removed temporal dir record '{}'", full_path);
        }
    }
    return diff.visit_next(*this, custom);
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
        auto folder_ctx = local_keeper::make_context(local_folder, diff.sub_dir, true);
        if (!folder_ctx) {
            LOG_ERROR(log, "cannot initialize scan of '{}'({})", diff.folder_id, diff.sub_dir);
            auto now = r::pt::microsec_clock::local_time();
            auto finish = model::diff::cluster_diff_ptr_t();
            finish = new model::diff::local::scan_finish_t(diff.folder_id, now);
            send<model::payload::model_update_t>(coordinator, std::move(finish));
        } else {
            lc_context_t(this, {}).new_contexts.emplace_back(std::move(folder_ctx));
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
    using queue_t = std::pmr::list<model::file_info_t *>;
    using F = presentation::presence_t::features_t;

    auto folder = local_folder.get_folder();
    auto folder_id = folder->get_id();
    auto new_name = proto::get_name(change);
    auto &prev_name = change.prev_path;
    LOG_DEBUG(log, "(input) handle rename '{}' -> '{}' in folder '{}'", prev_name, new_name, folder_id);
    auto &local_files = local_folder.get_file_infos();

    auto f_root = local_files.by_name(prev_name);
    if (!f_root) {
        auto it = stack_ctx.name_2_file.find(prev_name);
        if (it != stack_ctx.name_2_file.end()) {
            f_root = it->second;
        } else {
            LOG_WARN(log, "no '{}' in local folder", prev_name);
            return;
        }
    }

    auto assembler = model::diff::diff_assember_t(constants::diffs_batch);
    auto queue = queue_t(stack_ctx.allocator);
    queue.emplace_back(f_root.get());
    while (!queue.empty()) {
        auto f = queue.front();
        queue.pop_front();
        auto it = stack_ctx.file_2_name.find(f);
        auto p_name = [&]() -> std::string_view {
            if (it == stack_ctx.file_2_name.end()) {
                return f->get_name()->get_full_name();
            }
            return it->second;
        }();
        auto sub_name = std::pmr::string(stack_ctx.allocator);

        sub_name += new_name;
        sub_name += p_name.substr(prev_name.size());

        auto pr_new = f->as_proto(true);
        auto pr_prev = f->as_proto(false);
        proto::set_name(pr_prev, p_name);
        auto new_sub_name = std::string_view(sub_name);

        LOG_DEBUG(log, "renaming '{}' -> '{}' in folder '{}'", p_name, new_sub_name, folder_id);
        proto::set_name(pr_new, new_sub_name);
        proto::set_deleted(pr_prev, true);
        stack_ctx.push_back(new advance::local_update_t(*cluster, *sequencer, std::move(pr_new), folder_id));
        assembler.push_front(new advance::local_update_t(*cluster, *sequencer, std::move(pr_prev), folder_id, true));

        stack_ctx.name_2_file.emplace(new_sub_name, f);
        stack_ctx.file_2_name.insert_or_assign(f, new_sub_name);

        if (f->is_dir()) {
            auto folder_path = narrow(folder->get_path().generic_wstring());
            auto full_path = fmt::format("{}/{}", folder_path, new_sub_name);
            just_created_dirs.insert(std::move(full_path));

            auto presence = static_cast<presentation::presence_t *>(f->get_augmentation().get());
            for (auto c : presence->get_children()) {
                auto features = c->get_features();
                if (!(features & F::deleted) && !(features & F::missing) && (features & F::local)) {
                    auto cp = static_cast<presentation::cluster_file_presence_t *>(c);
                    auto file_info = const_cast<model::file_info_t *>(&cp->get_file_info());
                    queue.emplace_back(file_info);
                }
            }
        }
    }
    if (assembler.has_diffs()) {
        auto diff = assembler.consume();
        stack_ctx.push_back(diff.get());
    }
}

void local_keeper_t::on_changes(model::folder_info_t &local_folder, fs::payload::file_changes_t &changes,
                                lc_context_t &stack_ctx) noexcept {
    using namespace model::diff;
    using strings_t = std::pmr::unordered_set<std::pmr::string, utils::string_hash_t, utils::string_eq_t>;
    using I = syncspirit_watcher_impl_t;
    using CI = local_keeper::child_info_t;
    auto scheduled_dirs = strings_t(stack_ctx.allocator);

    auto folder = local_folder.get_folder();
    auto folder_id = folder->get_id();
    auto &files_map = local_folder.get_file_infos();
    auto unexamined = local_keeper::unexamined_items_t();
    auto augmentation = local_folder.get_augmentation().get();
    auto folder_presence = static_cast<presentation::folder_presence_t *>(augmentation);

    auto mk_full_path = [&](std::string_view name) -> std::pmr::string {
        auto folder_path = narrow(folder->get_path().generic_wstring());
        auto full_path = std::pmr::string(folder_path, stack_ctx.allocator);
        full_path += "/";
        full_path += name;
        return full_path;
    };
    auto immediate_update = [&](fs::payload::file_info_t &change) {
        auto name = proto::get_name(change);
        auto file = files_map.by_name(name);
        auto is_dir = proto::get_type(change) == proto::FileInfoType::DIRECTORY;
        bool update = true;
        if (file) {
            if (proto::get_deleted(change)) {
                // migrare relevant metadata
                proto::set_type(change, model::file_info_t::as_type(file->get_type()));
                proto::set_modified_s(change, file->get_modified_s());
                proto::set_modified_ns(change, file->get_modified_ns());
            } else {
                update = !file->identical_by_content_to(change);
            }
        } else {
            auto relation = folder_presence->get_link(name, is_dir);
            if (!relation.parent) {
                auto inside_subdir = name.find_last_of('/');
                if (inside_subdir != std::string::npos) {
                    auto subdir = name.substr(0, inside_subdir);
                    auto parent_path = mk_full_path(subdir);
                    update = just_created_dirs.count(parent_path);
                } else {
                    update = false;
                }
            } else {
                update = true;
            }
        }
        if (is_dir) {
            auto n = std::pmr::string(name, stack_ctx.allocator);
            scheduled_dirs.emplace(n);
        }
        if (update) {
            if (proto::get_type(change) == proto::FileInfoType::FILE) {
                if (fs::is_temporal(name)) {
                    auto seconds_ago = stack_ctx.get_now() - proto::get_modified_s(change);
                    if (seconds_ago < constants::tmp_min_age) {
                        LOG_DEBUG(log, "file '{}' is recently modified, ignoring", name);
                        return;
                    }
                }
            }

            auto renamed = (change.update_reason == UT::meta) && !change.prev_path.empty();
            if (renamed) {
                handle_rename(change, local_folder, stack_ctx);
            } else {
                if (is_dir && change.update_reason == UT::created) {
                    auto tmp = mk_full_path(name);
                    auto full_path = std::string(tmp.data(), tmp.size());
                    just_created_dirs.insert(full_path);
                }
                stack_ctx.push_back(new advance::local_update_t(*cluster, *sequencer, std::move(change), folder_id));
            }
        } else {
            LOG_DEBUG(log, "ignoring update on '{}'", name);
        }
    };
    auto delayed_update = [&](fs::payload::file_info_t change, bool recurse_children) {
        using R = presentation::presence_link_t;
        auto name = proto::get_name(change);
        auto is_dir = proto::get_type(change) == proto::FileInfoType::DIRECTORY;
        auto relation = !name.empty() ? folder_presence->get_link(name, is_dir) : R{{}, folder_presence};
        auto has_parent = relation.parent;
        auto inside_subdir = name.find_last_of('/');
        auto subdir = std::string_view{};
        if (inside_subdir != std::string::npos) {
            subdir = name.substr(0, inside_subdir);
        }
        if (!has_parent && !subdir.empty() && !scheduled_dirs.count(subdir)) {
            LOG_DEBUG(log, "no parent for '{}' in folder '{}', ignoring (temporal) orphan", name, folder_id);
            return;
        }
        if (change.requires_refinement && !subdir.empty() && scheduled_dirs.count(subdir)) {
            LOG_DEBUG(log, "ignoring change '{}' in folder '{}', parent dir scan is scheduled", name, folder_id);
            return;
        }
        auto path = folder->get_path();
        if (name.size()) {
            path /= widen(name);
        }
        auto child_info = CI(std::move(change), std::move(path), relation.child, relation.parent, 0);
        auto item = unexamined_t(std::move(child_info), true, recurse_children, change.requires_refinement);
        unexamined.push_back(std::move(item));
    };
    auto handle_delete = [&](fs::payload::file_info_t change) {
        auto name = proto::get_name(change);
        auto file = local_folder.get_file_infos().by_name(name);
        if (!file || file->is_deleted()) {
            LOG_DEBUG(log, "ignoring removal '{}' in folder '{}' as it is already missing", name, folder_id);
            return;
        }
        if (file->is_dir() && change.requires_refinement) {
            auto aug = file->get_augmentation().get();
            auto presence = static_cast<presentation::presence_t *>(aug);
            auto parent = presence->get_parent();
            if (!parent) {
                LOG_ERROR(log, "missing parent for '{}' in local folder '{}'", name, folder_id);
                return;
            }

            auto path = folder->get_path() / widen(parent->get_entity()->get_path()->get_full_name());
            auto child_name = bfs::path(widen(presence->get_entity()->get_path()->get_own_name()));
            auto item =
                unscanned_dir_t(std::move(path), parent, std::move(child_name), 0, true, change.requires_refinement);
            unexamined.push_back(std::move(item));
        } else {
            immediate_update(change);
        }
    };
    auto check_validity = [&](fs::payload::file_info_t &change) -> bool {
        auto name = proto::get_name(change);
        if (!utils::is_utf8_valid(name)) {
            auto name_hex = spdlog::to_hex(name.begin(), name.end());
            LOG_WARN(log, "invalid filename: '{}' in folder '{}' ", name_hex, folder_id);
            return false;
        }
        if (fs::is_temporal(name)) {
            LOG_WARN(log, "temporal file '{}' ignored in folder '{}' ", name, folder_id);
            return false;
        }
        auto link = proto::get_symlink_target(change);
        if (!link.empty() && !utils::is_utf8_valid(link)) {
            auto link_hex = spdlog::to_hex(link.begin(), link.end());
            LOG_WARN(log, "invalid symlink target of  : {} in folder '{}':\n{} ", name, folder_id, link_hex);
            return false;
        }
        return true;
    };
    for (auto &change : changes) {
        if (!check_validity(change)) {
            continue;
        }
        switch (change.update_reason) {
        case UT::created: {
            if (proto::get_type(change) == proto::FileInfoType::DIRECTORY) {
                if (change.requires_refinement) {
                    delayed_update(change, true);
                } else {
                    immediate_update(change);
                }
            } else {
                delayed_update(std::move(change), true);
            }
            break;
        }
        case UT::meta:
            immediate_update(change);
            break;
        case UT::deleted:
            handle_delete(change);
            break;
        case UT::content:
            delayed_update(std::move(change), false);
            break;
        default:
            LOG_WARN(log, "not implemented");
        }
    }
    if (!unexamined.empty()) {
        auto folder_ctx = local_keeper::make_context(&local_folder, std::move(unexamined));
        stack_ctx.new_contexts.emplace_back(std::move(folder_ctx));
    }
}
