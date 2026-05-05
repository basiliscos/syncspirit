// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_base.h"
#include "fs/utils.h"
#include "proto/proto-helpers-bep.h"
#include "net/names.h"
#include "model/messages.h"
#include <boost/nowide/convert.hpp>
#include <string.h>

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::fs::platform;
using boost::nowide::narrow;
using boost::nowide::widen;

static auto actor_identity = net::names::watcher;

using BU = watcher_base_t::bulk_update_t;
using FU = watcher_base_t::folder_update_t;

namespace {
namespace resource {
r::plugin::resource_id_t service = 0;
} // namespace resource
} // namespace

auto BU::prepare(std::string_view folder_id) noexcept -> folder_update_t & {
    for (auto &fi : updates) {
        if (fi.folder_id == folder_id) {
            return fi;
        }
    }
    return updates.emplace_back(folder_update_t{std::string(folder_id)});
};

auto BU::find(std::string_view folder_id) noexcept -> folder_update_t * {
    for (auto &fi : updates) {
        if (fi.folder_id == folder_id) {
            return &fi;
        }
    }
    return {};
};

bool BU::has_changes() const noexcept {
    for (auto &fi : updates) {
        if (!fi.updates.empty()) {
            return true;
        }
    }
    return false;
}

auto BU::make(const watched_folders_t &watched_folders, watcher_base_t &actor) noexcept -> folder_changes_opt_t {
    if (!deadline.is_not_a_date_time()) {
        auto r = payload::folder_changes_t();
        for (auto &fi : updates) {
            auto &folder_path = watched_folders.at(fi.folder_id);
            auto file_changes = fi.make(folder_path, actor);
            if (!file_changes.empty()) {
                auto change = payload::folder_change_t(std::move(fi.folder_id), std::move(file_changes));
                r.emplace_back(std::move(change));
            }
        }
        updates.resize(0);
        deadline = {};
        if (!r.empty()) {
            return std::move(r);
        }
    }
    return {};
}

bool FU::update(support::file_update_t &record, folder_update_t *prev) noexcept {
    if (auto it = updates.find(record.path); it != updates.end()) {
        return update(record, it, updates);
    }
    if (auto it = updates.find(record.prev_path); it != updates.end()) {
        return update(record, it, updates);
    }
    if (prev) {
        auto &updates = prev->updates;
        if (auto it = updates.find(record.path); it != updates.end()) {
            return update(record, it, updates);
        }
        if (auto it = updates.find(record.prev_path); it != updates.end()) {
            return update(record, it, updates);
        }
    }
    updates.emplace(std::move(record));
    return true;
}

bool FU::update(support::file_update_t &new_record, it_t it_prev, support::file_updates_t &prev_source) noexcept {
    namespace ut = update_type;
    static constexpr auto CONTENT_LIKE = (ut::CONTENT | ut::CREATED | ut::CREATED_1);
    auto log = utils::get_logger(actor_identity);
    auto &prev = *it_prev;
    if (prev.update_type == ut::META && new_record.update_type == ut::META) {
        if (prev.prev_path == new_record.path) {
            auto log = utils::get_logger(actor_identity);
            LOG_DEBUG(log, "collpasing renaming back-n-forth '{}'", new_record.path);
            return false;
        }
    }
    if ((prev.update_type & CONTENT_LIKE) && (new_record.update_type & ut::META)) {
        if (!new_record.prev_path.empty()) {
            LOG_DEBUG(log, "splitting event change + rename ('{}' => '{}') into delete + create", new_record.prev_path,
                      new_record.path);
            auto new_del =
                support::file_update_t(std::move(new_record.prev_path), {}, ut::DELETED, prev.requires_refinement);
            prev_source.erase(it_prev);
            updates.insert(std::move(new_del));
            new_record.update_type = ut::CREATED_1;
            new_record.prev_path = {};
            updates.emplace(std::move(new_record));
            return true;
        } else {
            new_record.update_type = prev.update_type;
            LOG_DEBUG(log, "discarding new meta changes in the sake of previous content of '{}'", new_record.path);
        }
    }
    if ((prev.update_type & ut::META && !prev.prev_path.empty()) && (new_record.update_type & CONTENT_LIKE)) {
        LOG_DEBUG(log, "splitting event rename + change ('{}' => '{}') into delete + create", prev.prev_path,
                  new_record.path);
        auto new_del = support::file_update_t(std::move(prev.prev_path), {}, ut::DELETED, prev.requires_refinement);
        prev_source.erase(it_prev);
        updates.insert(std::move(new_del));
        new_record.update_type = ut::CREATED_1;
        new_record.prev_path = {};
        updates.emplace(std::move(new_record));
        return true;
    }

    if (!prev.prev_path.empty()) {
        new_record.prev_path = std::move(prev.prev_path);
        LOG_DEBUG(log, "preserving prev path '{}'  for '{}'", new_record.prev_path, new_record.path);
    }
    if (prev.update_type & ut::CREATED_1) {
        new_record.update_type |= ut::CREATED_1;
        LOG_DEBUG(log, "preserving creation flag for '{}'", new_record.path);
    }
    if (prev.requires_refinement && !new_record.requires_refinement) {
        new_record.requires_refinement = true;
        LOG_DEBUG(log, "preserving refinement flag for '{}'", new_record.path);
    }

    prev_source.erase(it_prev);
    updates.emplace(std::move(new_record));
    return true;
}

bool FU::update(std::string_view relative_path, update_type_t type, folder_update_t *prev, std::string prev_path_rel,
                bool requires_refinement) noexcept {
    auto record_type =
        type == update_type_t::created ? update_type::CREATED_1 : static_cast<update_type_internal_t>(type);
    auto record =
        support::file_update_t(std::string(relative_path), std::move(prev_path_rel), record_type, requires_refinement);
    return update(record, prev);
}

auto FU::make(const folder_info_t &folder_info, watcher_base_t &actor) noexcept -> payload::file_changes_t {
    namespace ut = update_type;
    using UT = update_type_t;
    using FT = bfs::file_type;
    static const size_t SS_PATH_MAX = 32 * 1024;

    auto files = payload::file_changes_t();
    auto &mediator = *actor.updates_mediator;
    files.reserve(updates.size());
    auto log = utils::get_logger(actor_identity);
    char full_path[SS_PATH_MAX];
    auto folder_path_sz = folder_info.path_str.size();
    std::memcpy(full_path, folder_info.path_str.data(), folder_path_sz);
    for (auto &update : updates) {
        auto rel_name_ptr = full_path + folder_path_sz;
        auto &sub_path = update.path;
        auto rel_offset = 0;
        if (sub_path.size()) {
            rel_offset = 1;
            *rel_name_ptr++ = '/';
            std::memcpy(rel_name_ptr, update.path.data(), update.path.size());
        }
        auto rel_name_path_sz = folder_path_sz + update.path.size();
        *(full_path + rel_name_path_sz + rel_offset) = 0;
        auto full_name = std::string_view(full_path, rel_name_path_sz + rel_offset);
        if (mediator.is_masked(full_name)) {
            continue;
        }
        auto r = proto::FileInfo{};
        auto name = &update.path;
        auto prev_name = &update.prev_path;
        auto reason = UT{};
        if (update.update_type & ut::DELETED) {
            if (update.update_type & ut::CREATED_1) {
                LOG_DEBUG(log, "ignoring creation & deletion of '{}'", full_name);
                // created & deleted within retension interval => ignore
                continue;
            }
            proto::set_deleted(r, true);
            reason = UT::deleted;
            if (!update.prev_path.empty()) {
                name = &update.prev_path;
                prev_name = nullptr;
            }
        } else {
            auto ec = sys::error_code{};
            auto path = folder_info.path / widen(update.path);
            auto status = bfs::symlink_status(path, ec);
            if (ec) {
                LOG_DEBUG(log, "cannot get status on '{}': {} (update ignored)", full_name, ec.message());
                continue;
            }
            if (!actor.accept_update(update, status)) {
                LOG_TRACE(log, "skipping update on {}", full_name);
                continue;
            }
            proto::set_permissions(r, static_cast<uint32_t>(status.permissions()));
            if (status.type() == FT::regular) {
                auto sz = bfs::file_size(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot get size on '{}': {} (update ignored)", full_name, ec.message());
                    continue;
                }
                auto modified = bfs::last_write_time(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot get last_write_time on '{}': {} (update ignored)", full_name, ec.message());
                    continue;
                }
                proto::set_modified_s(r, to_unix(modified));
                proto::set_type(r, proto::FileInfoType::FILE);
                proto::set_size(r, static_cast<std::int64_t>(sz));
            } else if (status.type() == FT::directory) {
                proto::set_type(r, proto::FileInfoType::DIRECTORY);
            } else if (status.type() == FT::symlink) {
                auto target = bfs::read_symlink(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot read_symlink on '{}': {} (update ignored)", full_name, ec.message());
                    continue;
                }
                proto::set_symlink_target(r, narrow(target.generic_wstring()));
                proto::set_type(r, proto::FileInfoType::SYMLINK);
            } else {
                LOG_DEBUG(log, "ignoring '{}'", full_name);
                continue;
            }
            if (update.update_type & (ut::CREATED | ut::CREATED_1)) {
                reason = UT::created;
            } else if (update.update_type & ut::CONTENT) {
                reason = UT::content;
            } else {
                reason = UT::meta;
            }
        }
        proto::set_name(r, *name);
        auto info = payload::file_info_t(std::move(r), prev_name ? *prev_name : std::string(), reason,
                                         update.requires_refinement);
        files.push_back(std::move(info));
    }
    return files;
}

watcher_base_t::watcher_base_t(config_t &cfg)
    : parent_t{cfg}, retension(cfg.change_retension), updates_mediator{std::move(cfg.updates_mediator)},
      watched_folders(cfg.watched_folders), fs_config{cfg.fs_config} {
    log = utils::get_logger(actor_identity);
    if (!retension.is_positive()) {
        LOG_ERROR(log, "retension interval should be positive");
        throw std::runtime_error("retension interval should be positive");
    }
    assert(updates_mediator);
    assert(watched_folders);
}

void watcher_base_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(actor_identity, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(actor_identity, get_address());
        p.discover_name(net::names::coordinator, coordinator, false).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&watcher_base_t::on_watch);
        p.subscribe_actor(&watcher_base_t::on_unwatch);
    });
}

void watcher_base_t::on_start() noexcept {
    LOG_TRACE(log, "on_start");
    send<model::payload::local_up_t>(coordinator);
    r::actor_base_t::on_start();
}

void watcher_base_t::on_watch(message::watch_folder_t &) noexcept {
    LOG_WARN(log, "watching directory isn't supported by platform");
}

void watcher_base_t::on_unwatch(message::unwatch_folder_t &) noexcept {
    LOG_WARN(log, "unwatching directory isn't supported by platform");
}

void watcher_base_t::on_service_lock(model::message::service_lock_t &message) noexcept {
    if (message.payload.service == net::names::watcher) {
        LOG_DEBUG(log, "on_service_lock");
        resources->acquire(resource::service);
    }
}

void watcher_base_t::on_service_unlock(model::message::service_unlock_t &message) noexcept {
    if (message.payload.service == net::names::watcher) {
        LOG_DEBUG(log, "on_service_unlock");
        resources->release(resource::service);
    }
}

void watcher_base_t::push(const timepoint_t &deadline, std::string_view folder_id, std::string_view relative_path,
                          std::string prev_path, update_type_t type, bool requires_refinement) noexcept {
    auto source = (bulk_update_t *)(nullptr);
    auto target = (bulk_update_t *)(nullptr);
    LOG_DEBUG(log, "file event '{}' for '{}' in folder {}", support::stringify(type), relative_path, folder_id);
    if (next.deadline == deadline) {
        target = &next;
    } else if (next.deadline.is_not_a_date_time()) {
        target = &next;
        LOG_DEBUG(log, "starting retension timer...");
        start_timer(retension, *this, &watcher_base_t::on_retension_finish);
    } else {
        source = &next;
        target = &postponed;
    }
    auto &target_fi = target->prepare(folder_id);
    auto source_fi = source ? source->find(folder_id) : (folder_update_t *)(nullptr);
    auto recorded = target_fi.update(relative_path, type, source_fi, std::move(prev_path), requires_refinement);
    if (recorded) {
        if (target->deadline.is_not_a_date_time()) {
            target->deadline = deadline;
        }
    }
}

void watcher_base_t::on_retension_finish(r::request_id_t, bool cancelled) noexcept {
    LOG_TRACE(log, "on_retension_finish ({} ms)", retension.total_milliseconds());
    if (!cancelled) {
        auto opt = next.make(*watched_folders, *this);
        if (opt) {
            LOG_DEBUG(log, "sending changes");
            send<payload::folder_changes_t>(coordinator, std::move(opt).value());
        }
        if (postponed.has_changes()) {
            LOG_DEBUG(log, "scheduling next changes");
            next = std::move(postponed);
            next.deadline = clock_t::local_time() + retension;
            start_timer(retension, *this, &watcher_base_t::on_retension_finish);
        }
    }
}

bool watcher_base_t::accept_update(const support::file_update_t &, const bfs::file_status &) noexcept { return true; }

void watcher_base_t::notify(const fs::task::scan_dir_t &) noexcept {}
