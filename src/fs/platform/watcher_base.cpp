// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "watcher_base.h"
#include "fs/utils.h"
#include "proto/proto-helpers-bep.h"
#include "net/names.h"
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::fs::platform;
using boost::nowide::narrow;
using boost::nowide::widen;

static auto actor_identity = net::names::watcher;

using BU = watcher_base_t::bulk_update_t;
using FU = watcher_base_t::folder_update_t;
using FU_EQ = watcher_base_t::file_update_eq_t;
using Hash = watcher_base_t::file_update_hash_t;

static std::string_view stringify(payload::update_type_t type) {
    using U = payload::update_type_t;
    if (type == U::created) {
        return "created";
    } else if (type == U::deleted) {
        return "deleted";
    } else if (type == U::meta) {
        return "metadata changed";
    } else {
        return "content changed";
    }
}

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

auto BU::make(const folder_map_t &folder_map) noexcept -> folder_changes_opt_t {
    if (!deadline.is_not_a_date_time()) {
        auto r = payload::folder_changes_t();
        for (auto &fi : updates) {
            auto &folder_path = folder_map.at(fi.folder_id);
            auto file_changes = fi.make(folder_path);
            if (!file_changes.empty()) {
                auto change = payload::folder_change_t(std::move(fi.folder_id), std::move(file_changes));
                r.emplace_back(std::move(change));
            }
        }
        updates.resize(0);
        if (!r.empty()) {
            deadline = {};
            return std::move(r);
        }
    }
    return {};
}

void FU::update(std::string_view relative_path, update_type_t type, folder_update_t *prev) noexcept {
    auto it = updates.find(relative_path);
    auto internal = static_cast<payload::update_type_internal_t>(type);
    if (prev) {
        auto &updates = prev->updates;
        if (auto it = updates.find(relative_path); it != updates.end()) {
            auto ut = it->update_type;
            if (ut & payload::update_type::CREATED_1) {
                internal = internal | payload::update_type::CREATED_1;
            }
            if ((ut & payload::update_type::CONTENT) && (type == update_type_t::meta)) {
                internal = ut;
            }
            updates.erase(it);
        }
    }
    if (it == updates.end()) {
        if (type == update_type_t::created) {
            internal = payload::update_type::CREATED_1;
        }
        updates.emplace(file_update_t{std::string(relative_path), internal});
    } else {
        it->update_type = (it->update_type & payload::update_type::CREATED_1) | internal;
    }
}

auto FU::make(const bfs::path &folder_path) noexcept -> payload::file_changes_t {
    auto files = payload::file_changes_t();
    files.reserve(updates.size());
    auto log = utils::get_logger(actor_identity);
    for (auto &update : updates) {
        namespace ut = payload::update_type;
        using UT = payload::update_type_t;
        using FT = bfs::file_type;
        auto r = payload::file_info_t{};
        if (update.update_type & ut::DELETED) {
            if (update.update_type & ut::CREATED_1) {
                // created & deleted within retension interval => ignoore
                continue;
            }
            proto::set_deleted(r, true);
        } else {
            auto ec = sys::error_code{};
            auto path = folder_path / widen(update.path);
            auto status = bfs::symlink_status(path, ec);
            if (ec) {
                LOG_WARN(log, "cannot get status on '{}': {} (update ignored)", narrow(path.generic_wstring()),
                         ec.message());
                continue;
            }
            proto::set_permissions(r, static_cast<uint32_t>(status.permissions()));
            if (status.type() == FT::regular) {
                auto sz = bfs::file_size(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot get size on '{}': {} (update ignored)", narrow(path.generic_wstring()),
                             ec.message());
                    continue;
                }
                auto modified = bfs::last_write_time(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot get last_write_time on '{}': {} (update ignored)",
                             narrow(path.generic_wstring()), ec.message());
                    continue;
                }
                proto::set_modified_s(r, to_unix(modified));
                proto::set_type(r, proto::FileInfoType::FILE);
                proto::set_size(r, static_cast<std::int64_t>(sz));
                r.only_meta_changed = update.update_type & ut::META;
            } else if (status.type() == FT::directory) {
                proto::set_type(r, proto::FileInfoType::DIRECTORY);
            } else if (status.type() == FT::symlink) {
                auto target = bfs::read_symlink(path, ec);
                if (ec) {
                    LOG_WARN(log, "cannot read_symlink on '{}': {} (update ignored)", narrow(path.generic_wstring()),
                             ec.message());
                    continue;
                }
                proto::set_symlink_target(r, narrow(target.generic_wstring()));
                proto::set_type(r, proto::FileInfoType::SYMLINK);
            } else {
                LOG_DEBUG(log, "ignoring '{}'", narrow(path.generic_wstring()));
                continue;
            }
        }
        proto::set_name(r, std::move(update.path));
        files.emplace_back(std::move(r));
    }
    return files;
}

size_t Hash::operator()(const file_update_t &file_update) const noexcept {
    auto path = std::string_view(file_update.path);
    return (*this)(path);
}

size_t Hash::operator()(std::string_view path) const noexcept { return std::hash<std::string_view>()(path); }

bool FU_EQ::operator()(const file_update_t &lhs, const file_update_t &rhs) const noexcept {
    return lhs.path == rhs.path;
}
bool FU_EQ::operator()(const file_update_t &lhs, std::string_view rhs) const noexcept { return lhs.path == rhs; }
bool FU_EQ::operator()(std::string_view lhs, const file_update_t &rhs) const noexcept { return lhs == rhs.path; }

watcher_base_t::watcher_base_t(config_t &cfg) : parent_t{cfg}, retension(cfg.change_retension) {
    log = utils::get_logger(actor_identity);
    if (!retension.is_positive()) {
        LOG_ERROR(log, "retension interval should be positive");
        throw std::runtime_error("retension interval should be positive");
    }
}

void watcher_base_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(actor_identity, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(actor_identity, get_address());
        p.discover_name(net::names::coordinator, coordinator, false).link(false);
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) { p.subscribe_actor(&watcher_base_t::on_watch); });
}

void watcher_base_t::on_watch(message::watch_folder_t &) noexcept {
    LOG_WARN(log, "watching directory isn't supported by platform");
}

void watcher_base_t::push(const timepoint_t &deadline, std::string_view folder_id, std::string_view relative_path,
                          update_type_t type) noexcept {
    auto source = (bulk_update_t *)(nullptr);
    auto target = (bulk_update_t *)(nullptr);
    LOG_DEBUG(log, "file event '{}' for '{}' in folder {}", stringify(type), relative_path, folder_id);
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
    target_fi.update(relative_path, type, source_fi);
    if (target->deadline.is_not_a_date_time()) {
        target->deadline = deadline;
    }
}

void watcher_base_t::on_retension_finish(r::request_id_t, bool cancelled) noexcept {
    LOG_TRACE(log, "on_retension_finish");
    if (!cancelled) {
        auto opt = next.make(folder_map);
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
