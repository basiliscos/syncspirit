// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_base.h"
#include "fs/utils.h"
#include "proto/proto-helpers-bep.h"
#include "net/names.h"
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

auto BU::make(const folder_map_t &folder_map, updates_mediator_t &mediator) noexcept -> folder_changes_opt_t {
    if (!deadline.is_not_a_date_time()) {
        auto r = payload::folder_changes_t();
        for (auto &fi : updates) {
            auto &folder_path = folder_map.at(fi.folder_id);
            auto file_changes = fi.make(folder_path, mediator);
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

void FU::update(std::string_view relative_path, update_type_t type, folder_update_t *prev,
                std::string prev_path_rel) noexcept {
    auto it = updates.find(relative_path);
    auto it_prev = (typename decltype(updates)::const_iterator){};
    auto prev_update = (const support::file_update_t *)(nullptr);
    if (prev) {
        auto &updates = prev->updates;
        auto target = std::string_view(prev_path_rel.empty() ? relative_path : prev_path_rel);
        it_prev = prev->updates.find(target);
        if (it_prev != prev->updates.end()) {
            prev_update = &*it_prev;
        }
#if 0
        if (auto it = updates.find(relative_path); it != updates.end()) {
            auto ut = it->update_type;
            if (ut & update_type::CREATED_1) {
                internal = internal | update_type::CREATED_1;
            }
            if ((ut & update_type::CONTENT) && (type == update_type_t::meta)) {
                internal = ut;
            }
            updates.erase(it);
        }
#endif
    }
#if 0
    auto internal = static_cast<update_type_internal_t>(type);
    if (it == updates.end()) {
        if (type == update_type_t::created) {
            internal = update_type::CREATED_1;
        }
        updates.emplace(support::file_update_t{std::string(relative_path), std::move(prev_path), internal});
    } else {
        it->update_type = (it->update_type & update_type::CREATED_1) | internal;
    }
#endif
    if (it != updates.end()) {
        it->update(prev_path_rel, type);
    } else {
        auto update = support::file_update_t(std::string(relative_path), std::move(prev_path_rel), type, prev_update);
        updates.emplace(std::move(update));
    }
    if (prev_update) {
        prev->updates.erase(it_prev);
    }
}

auto FU::make(const folder_info_t &folder_info, updates_mediator_t &mediator) noexcept -> payload::file_changes_t {
    namespace ut = update_type;
    using UT = update_type_t;
    using FT = bfs::file_type;
    static const size_t SS_PATH_MAX = 32 * 1024;

    auto files = payload::file_changes_t();
    files.reserve(updates.size());
    auto log = utils::get_logger(actor_identity);
    char full_path[SS_PATH_MAX];
    auto folder_path_sz = folder_info.path_str.size();
    std::memcpy(full_path, folder_info.path_str.data(), folder_path_sz);
    for (auto &update : updates) {
        auto rel_name_ptr = full_path + folder_path_sz;
        *rel_name_ptr++ = '/';
        std::memcpy(rel_name_ptr, update.path.data(), update.path.size());
        auto rel_name_path_sz = folder_path_sz + update.path.size();
        *(full_path + rel_name_path_sz + 1) = 0;
        auto full_name = std::string_view(full_path, rel_name_path_sz + 1);
        if (mediator.is_masked(full_name)) {
            continue;
        }
        auto r = proto::FileInfo{};
        auto name = &update.path;
        auto prev_name = &update.prev_path;
        auto reason = UT{};
        if (update.update_type & ut::DELETED) {
            if (update.update_type & ut::CREATED_1) {
                // created & deleted within retension interval => ignoore
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
                if (update.update_type == ut::CONTENT) {
                    LOG_DEBUG(log, "ignoring content changes in dir '{}'", full_name);
                    continue;
                }
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
        files.emplace_back(payload::file_info_t(std::move(r), prev_name ? *prev_name : std::string(), reason));
    }
    return files;
}

watcher_base_t::watcher_base_t(config_t &cfg)
    : parent_t{cfg}, retension(cfg.change_retension), updates_mediator{std::move(cfg.updates_mediator)} {
    log = utils::get_logger(actor_identity);
    if (!retension.is_positive()) {
        LOG_ERROR(log, "retension interval should be positive");
        throw std::runtime_error("retension interval should be positive");
    }
    assert(updates_mediator);
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

void watcher_base_t::on_watch(message::watch_folder_t &) noexcept {
    LOG_WARN(log, "watching directory isn't supported by platform");
}

void watcher_base_t::on_unwatch(message::unwatch_folder_t &) noexcept {
    LOG_WARN(log, "unwatching directory isn't supported by platform");
}

void watcher_base_t::push(const timepoint_t &deadline, std::string_view folder_id, std::string_view relative_path,
                          std::string prev_path, update_type_t type) noexcept {
    auto source = (bulk_update_t *)(nullptr);
    auto target = (bulk_update_t *)(nullptr);
    LOG_DEBUG(log, "file event '{}' for '{}' in folder {}", support::stringify(type), relative_path, folder_id);
    if (next.deadline == deadline) {
        target = &next;
    } else if (next.deadline.is_not_a_date_time()) {
        target = &next;
        start_timer(retension, *this, &watcher_base_t::on_retension_finish);
    } else {
        source = &next;
        target = &postponed;
    }
    auto &target_fi = target->prepare(folder_id);
    auto source_fi = source ? source->find(folder_id) : (folder_update_t *)(nullptr);
    target_fi.update(relative_path, type, source_fi, std::move(prev_path));
    if (target->deadline.is_not_a_date_time()) {
        target->deadline = deadline;
    }
}

void watcher_base_t::on_retension_finish(r::request_id_t, bool cancelled) noexcept {
    LOG_TRACE(log, "on_retension_finish");
    if (!cancelled) {
        auto opt = next.make(folder_map, *updates_mediator);
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
