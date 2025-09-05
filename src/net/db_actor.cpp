// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "db_actor.h"
#include "names.h"
#include "db/prefix.h"
#include "db/utils.h"
#include "hasher/messages.h"
#include "db/error_code.h"
#include "messages.h"
#include "model/diff/advance/advance.h"
#include "model/diff/contact/ignored_connected.h"
#include "model/diff/contact/peer_state.h"
#include "model/diff/contact/unknown_connected.h"
#include "model/diff/load/blocks.h"
#include "model/diff/load/commit.h"
#include "model/diff/load/devices.h"
#include "model/diff/load/file_infos.h"
#include "model/diff/load/folder_infos.h"
#include "model/diff/load/folders.h"
#include "model/diff/load/ignored_devices.h"
#include "model/diff/load/ignored_folders.h"
#include "model/diff/load/interrupt.h"
#include "model/diff/load/load_cluster.h"
#include "model/diff/load/pending_devices.h"
#include "model/diff/load/pending_folders.h"
#include "model/diff/load/remove_corrupted_files.h"
#include "model/diff/modify/add_blocks.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/add_pending_device.h"
#include "model/diff/modify/add_pending_folders.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/remove_files.h"
#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/remove_folder_infos.h"
#include "model/diff/modify/remove_ignored_device.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/remove_pending_device.h"
#include "model/diff/modify/remove_pending_folders.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/peer/update_folder.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"
#include "utils/format.hpp"
#include <string_view>
#include <cstring>

namespace syncspirit::net {

namespace {
namespace resource {
r::plugin::resource_id_t db = 0;
r::plugin::resource_id_t controller = 1;
r::plugin::resource_id_t partial_load = 2;
} // namespace resource
} // namespace

#if 0
static void _my_log(MDBX_log_level_t loglevel, const char *function,int line, const char *fmt, va_list args) noexcept
{
    vprintf(fmt, args);
}
#endif

using folder_infos_set_t = std::unordered_set<const model::folder_info_t *>;

db_actor_t::payload::commit_t::commit_t(db::transaction_t txn_) noexcept : txn{std::move(txn_)} {
    thread_id = std::this_thread::get_id();
}

outcome::result<void> db_actor_t::payload::commit_t::commit() noexcept {
    thread_id = {};
    return txn.commit();
}

db_actor_t::payload::commit_t::~commit_t() {
    if (thread_id != thread_id_t{}) {
        if (thread_id != std::this_thread::get_id()) {
            spdlog::error("attempt to close db orphaned transaction from other thread. Leak!");
            txn.txn = {};
        } else {
            auto r = txn.commit();
            if (r) {
                spdlog::debug("successfully closed orphaned transaction");
            } else {
                auto ec = r.assume_error();
                spdlog::debug("cannot close orphaned transaction: {}", ec.message());
            }
        }
    }
}

db_actor_t::db_actor_t(config_t &config)
    : r::actor_base_t{config}, env{nullptr}, db_dir{config.db_dir}, db_config{config.db_config},
      cluster{config.cluster} {
    // mdbx_module_handler({}, {}, {});
    // mdbx_setup_debug(MDBX_LOG_TRACE, MDBX_DBG_ASSERT, &_my_log);
    auto r = mdbx_env_create(&env);
    if (r != MDBX_SUCCESS) {
        auto log = utils::get_logger("net.db");
        LOG_CRITICAL(log, "mdbx environment creation error ({}): {}", r, mdbx_strerror(r));
        throw std::runtime_error(std::string(mdbx_strerror(r)));
    }
}

db_actor_t::~db_actor_t() {
    if (env) {
        mdbx_env_close(env);
    }
}

void db_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(names::db, false);
        log = utils::get_logger(identity);
        sink = p.create_address();
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::db, get_address());
        p.discover_name(net::names::bouncer, bouncer, true).link(false);
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&db_actor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&db_actor_t::on_db_info, coordinator);
                plugin->subscribe_actor(&db_actor_t::on_controller_up, coordinator);
                plugin->subscribe_actor(&db_actor_t::on_controller_down, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        open();
        p.subscribe_actor(&db_actor_t::on_cluster_load_trigger);
        p.subscribe_actor(&db_actor_t::on_commit);
        p.subscribe_actor(&db_actor_t::on_patrial_load);
    });
}

void db_actor_t::open() noexcept {
    resources->acquire(resource::db);
    auto &my_device = cluster->get_device();
    auto upper_limit = db_config.upper_limit;
    /* enable automatic size management */
    LOG_INFO(log, "open, db upper limit = {}", upper_limit);
    auto r = mdbx_env_set_geometry(env, -1, -1, upper_limit, -1, -1, -1);
    if (r != MDBX_SUCCESS) {
        LOG_ERROR(log, "open, mdbx set geometry error ({}): {}", r, mdbx_strerror(r));
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }

    auto flags = MDBX_WRITEMAP | MDBX_LIFORECLAIM | MDBX_EXCLUSIVE | MDBX_NOSTICKYTHREADS | MDBX_SAFE_NOSYNC;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    auto db_dir_w = db_dir.wstring();
    r = mdbx_env_openW(env, db_dir_w.c_str(), flags, 0664);
#else
    r = mdbx_env_open(env, db_dir.c_str(), flags, 0664);
#endif
    if (r != MDBX_SUCCESS) {
        LOG_ERROR(log, "open, mdbx open environment error ({}): {}, path: {}", r, mdbx_strerror(r), db_dir.string());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto txn = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn) {
        LOG_ERROR(log, "open, cannot create transaction {}", txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }

    auto db_ver = db::get_version(txn.value());
    if (!db_ver) {
        LOG_ERROR(log, "open, cannot get db version :: {}", db_ver.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto version = db_ver.value();
    LOG_DEBUG(log, "got db version: {}, expected : {} ", version, db::version);

    if (!txn) {
        LOG_ERROR(log, "open, cannot create transaction {}", txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    if (db_ver.value() != db::version) {
        txn = db::make_transaction(db::transaction_type_t::RW, txn.value());
        auto r = db::migrate(version, my_device, txn.value());
        if (!r) {
            LOG_ERROR(log, "open, cannot migrate db {}", r.error().message());
            resources->release(resource::db);
            return do_shutdown(make_error(r.error()));
        }
        LOG_INFO(log, "open, successfully migrated db: {} -> {} ", version, db::version);
    }
    resources->release(resource::db);
}

auto db_actor_t::get_txn() noexcept -> outcome::result<db::transaction_t *> {
    if (!txn_holder) {
        auto txn = db::make_transaction(db::transaction_type_t::RW, env);
        if (!txn) {
            return txn.assume_error();
        }
        txn_holder.reset(new db::transaction_t(std::move(txn.assume_value())));
        uncommitted = 0;
    }
    return txn_holder.get();
}

auto db_actor_t::commit_on_demand() noexcept -> outcome::result<void> {
    if (txn_holder && (++uncommitted >= db_config.uncommitted_threshold)) {
        LOG_DEBUG(log, "committing tx");
        auto r = txn_holder->commit();
        txn_holder.reset();
        return r;
    }
    return outcome::success();
}

auto db_actor_t::force_commit() noexcept -> outcome::result<void> {
    uncommitted = db_config.uncommitted_threshold;
    return outcome::success();
}

void db_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_start, triggering cluster loading");
    send<net::payload::load_cluster_trigger_t>(address);
}

void db_actor_t::shutdown_start() noexcept {
    r::actor_base_t::shutdown_start();
    LOG_TRACE(log, "shutdown_start");
}

void db_actor_t::shutdown_finish() noexcept {
    LOG_TRACE(log, "shutdown_finish");
    if (txn_holder && uncommitted) {
        uncommitted = db_config.uncommitted_threshold;
        auto r = commit_on_demand();
        if (!r) {
            auto &err = r.assume_error();
            LOG_ERROR(log, "cannot commit tx: {}", err.message());
        }
        txn_holder.reset();
    }
    auto r = mdbx_env_close(env);
    if (r != MDBX_SUCCESS) {
        LOG_ERROR(log, "open, mdbx close error ({}): {}", r, mdbx_strerror(r));
    }
    env = nullptr;
    r::actor_base_t::shutdown_finish();
}

void db_actor_t::on_db_info(message::db_info_request_t &request) noexcept {
    LOG_TRACE(log, "on_db_info");
    MDBX_stat stat;
    std::memset(&stat, 0, sizeof(stat));
    auto r = mdbx_env_stat_ex(env, nullptr, &stat, sizeof(stat));
    if (r != MDBX_SUCCESS) {
        LOG_ERROR(log, "mdbx_env_stat_ex, mdbx error ({}): {}", r, mdbx_strerror(r));
        auto ec = db::make_error_code(r);
        return reply_with_error(request, make_error(ec));
    }

    net::payload::db_info_response_t info{
        stat.ms_psize, stat.ms_depth, stat.ms_leaf_pages, stat.ms_overflow_pages, stat.ms_branch_pages, stat.ms_entries,
    };

    reply_to(request, info);
}

void db_actor_t::on_controller_up(net::message::controller_up_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_up, {}", (const void *)message.payload.controller.get());
    resources->acquire(resource::controller);
}

void db_actor_t::on_controller_down(net::message::controller_down_t &message) noexcept {
    LOG_DEBUG(log, "on_controller_down, {}", (const void *)message.payload.controller.get());
    resources->release(resource::controller);
}

void db_actor_t::on_cluster_load_trigger(message::load_cluster_trigger_t &) noexcept {
    using namespace model::diff;
    r::actor_base_t::on_start();
    LOG_TRACE(log, "on_cluster_load_trigger");

    auto txn_opt = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn_opt) {
        auto ee = make_error(txn_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }
    auto &txn = txn_opt.value();

    auto devices_opt = db::load(db::prefix::device, txn);
    if (!devices_opt) {
        auto ee = make_error(devices_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    auto blocks_opt = db::load(db::prefix::block_info, txn);
    if (!blocks_opt) {
        auto ee = make_error(blocks_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }
    auto blocks = std::move(blocks_opt.value());

    auto ignored_devices_opt = db::load(db::prefix::ignored_device, txn);
    if (!ignored_devices_opt) {
        auto ee = make_error(ignored_devices_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    auto ignored_folders_opt = db::load(db::prefix::ignored_folder, txn);
    if (!ignored_folders_opt) {
        auto ee = make_error(ignored_folders_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    auto folders_opt = db::load(db::prefix::folder, txn);
    if (!folders_opt) {
        auto ee = make_error(folders_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    auto folder_infos_opt = db::load(db::prefix::folder_info, txn);
    if (!folder_infos_opt) {
        auto ee = make_error(folder_infos_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }
    auto folder_infos_raw = std::move(folder_infos_opt.value());
    auto folder_infos_uuids = folder_infos_uuids_t();
    auto folder_infos = load::folder_infos_t::container_t();
    for (auto &pair : folder_infos_raw) {
        using item_t = load::folder_infos_t::item_t;
        auto decomposed = model::folder_info_t::decompose_key(pair.key);
        auto &uuid = decomposed.folder_info_id;
        auto b = reinterpret_cast<const char *>(uuid.data());
        auto e = b + uuid.size();
        auto folder_uuid = std::string(b, e);
        folder_infos_uuids.emplace(std::move(folder_uuid));
        auto db_fi = db::FolderInfo();
        if (auto left = db::decode(pair.value, db_fi); left) {
            auto ec = make_error_code(model::error_code_t::folder_info_deserialization_failure);
            return send<net::payload::load_cluster_fail_t>(coordinator, make_error(ec));
        }
        folder_infos.emplace_back(item_t{pair.key, std::move(db_fi)});
    }

    auto file_infos_opt = db::load(db::prefix::file_info, txn);
    if (!file_infos_opt) {
        auto ee = make_error(file_infos_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }
    auto files = std::move(file_infos_opt.value());

    auto pending_devices_opt = db::load(db::prefix::pending_device, txn);
    if (!pending_devices_opt) {
        auto ee = make_error(pending_devices_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    auto pending_folders_opt = db::load(db::prefix::pending_folder, txn);
    if (!pending_folders_opt) {
        auto ee = make_error(pending_folders_opt.error());
        return send<net::payload::load_cluster_fail_t>(coordinator, ee);
    }

    LOG_INFO(log, "on_cluster_load, all raw bytes has been loaded, blocks = {}, files = {}", blocks.size(),
             files.size());

    auto diff = model::diff::cluster_diff_ptr_t{};
    diff.reset(new load::load_cluster_t(blocks.size(), files.size()));

    auto current = diff->assign_child(new load::devices_t(std::move(devices_opt.value())))
                       ->assign_sibling(new load::ignored_devices_t(std::move(ignored_devices_opt.value())))
                       ->assign_sibling(new load::ignored_folders_t(std::move(ignored_folders_opt.value())))
                       ->assign_sibling(new load::folders_t(std::move(folders_opt.value())))
                       ->assign_sibling(new load::folder_infos_t(std::move(folder_infos)))
                       ->assign_sibling(new load::pending_devices_t(std::move(pending_devices_opt.value())))
                       ->assign_sibling(new load::pending_folders_t(std::move(pending_folders_opt.value())));

    auto known_hashes = known_hashes_t();
    auto blocks_next = db::container_t::pointer(nullptr);
    auto files_next = db::container_t::pointer(nullptr);
    if (blocks.size()) {
        known_hashes.reserve(blocks.size());
        blocks_next = blocks.data();
    }
    if (files.size()) {
        files_next = files.data();
    }

    auto p = payload::partial_load_t{
        std::move(diff),
        current,
        std::move(known_hashes),
        std::move(blocks),
        blocks_next,
        std::move(files),
        files_next,
        std::move(folder_infos_uuids),
        {},
        std::move(txn),
    };

    auto message = r::make_message<payload::partial_load_t>(address, std::move(p));
    send<hasher::payload::package_t>(bouncer, std::move(message));
    resources->acquire(resource::partial_load);
}

void db_actor_t::on_patrial_load(partial_load_t &message) noexcept {
    using namespace model::diff;
    auto &p = message.payload;
    bool bounce_again = false;

    if (state > r::state_t::OPERATIONAL) {
        LOG_DEBUG(log, "interrupting partial loading, committing read txn");

        auto ee = shutdown_reason;
        auto r = p.txn.commit();
        if (!r) {
            ee = make_error(r.assume_error());
            LOG_ERROR(log, "committing txn error: {}", r.assume_error().message());
        }
        send<net::payload::load_cluster_fail_t>(coordinator, ee);
        resources->release(resource::partial_load);
        return;
    }

    if (p.block_next) {
        bounce_again = true;
        auto max_blocks = db_config.max_blocks_per_diff;
        auto ptr = p.block_next;
        auto begin = p.blocks.data();
        auto end = begin + p.blocks.size();
        assert(ptr < end);
        auto chunk_end = std::min(ptr + max_blocks, end);
        auto slice = load::blocks_t::container_t();
        auto number = chunk_end - ptr;
        slice.reserve(number);
        LOG_INFO(log, "on_patrial_load, unpacking {} blocks ({} of {} are done)", number, ptr - begin, p.blocks.size());
        while (ptr < chunk_end) {
            auto &pair = *ptr;
            auto db_block = db::BlockInfo();
            if (auto left = db::decode(pair.value, db_block); left) {
                LOG_ERROR(log, "deserializing block failure, {} bytes left", left);
            } else {
                auto hash = pair.key.subspan(1);
                p.known_hashes.emplace(hash);
                slice.emplace_back(pair.key, std::move(db_block));
            }
            ++ptr;
        }
        p.next = p.next->assign_sibling(new load::blocks_t(std::move(slice)));
        if (ptr < end) {
            p.next = p.next->assign_sibling(new load::interrupt_t());
            p.block_next = ptr;
        } else {
            p.block_next = {};
        }
    }
    if (bounce_again) {
        return send<hasher::payload::package_t>(bouncer, &message);
    }

    if (p.files_next) {
        bounce_again = true;
        auto max_files = db_config.max_files_per_diff;
        auto ptr = p.files_next;
        auto begin = p.files.data();
        auto end = begin + p.files.size();
        assert(ptr < end);
        auto chunk_end = std::min(ptr + max_files, end);
        auto count = chunk_end - ptr;
        auto items = load::file_infos_t::container_t();
        LOG_INFO(log, "on_patrial_load, unpacking {} files ({} of {} are done)", count, ptr - begin, p.files.size());
        items.reserve(count);
        while (ptr != chunk_end) {
            using item_t = decltype(items)::value_type;
            auto data = ptr->value;
            auto db_fi = db::FileInfo();
            if (auto left = db::decode(ptr->value, db_fi); left) {
                LOG_ERROR(log, "deserializing file failure, {} bytes left", left);
            } else {
                auto success = true;
                auto name = db::get_name(db_fi);
                auto blocks_count = db::get_blocks_size(db_fi);
                for (size_t i = 0; i < blocks_count; ++i) {
                    auto block_hash = db::get_blocks(db_fi, i);
                    auto it = p.known_hashes.find(block_hash);
                    if (it == p.known_hashes.end()) {
                        LOG_INFO(log, "block #{} '{}' is missing file '{}', assuming corruped", i, block_hash, name);
                        success = false;
                        break;
                    }
                }

                auto key = ptr->key;
                if (success) {
                    auto folder_info_uuid_raw = key.subspan(1, model::uuid_length);
                    auto b = reinterpret_cast<const char *>(folder_info_uuid_raw.data());
                    auto e = b + folder_info_uuid_raw.size();
                    auto folder_info_uuid = std::string(b, e);
                    if (p.folder_infos_uuids.count(folder_info_uuid) == 0) {
                        LOG_INFO(log, "cannot restore file '{}', missing folder, assuming corruped", name);
                        success = false;
                    }
                }
                if (success) {
                    items.emplace_back(item_t{key, std::move(db_fi)});
                } else {
                    p.corrupted_files.emplace(utils::bytes_t(key));
                }
            }
            ++ptr;
        }
        p.next = p.next->assign_sibling(new load::file_infos_t(std::move(items)));
        if (ptr < end) {
            p.next = p.next->assign_sibling(new load::interrupt_t());
            p.files_next = ptr;
        } else {
            p.files_next = {};
        }
    }

    if (bounce_again) {
        return send<hasher::payload::package_t>(bouncer, &message);
    } else {
        auto &corrupted = p.corrupted_files;
        if (corrupted.size()) {
            LOG_WARN(log, "{} corrupted files will be removed", corrupted.size());
            p.next = p.next->assign_sibling(new load::remove_corrupted_files_t(std::move(corrupted)));
        }

        auto commit_message = r::make_routed_message<payload::commit_t>(sink, address, std::move(p.txn));
        p.next = p.next->assign_sibling(new load::commit_t(std::move(commit_message)));
        send<net::payload::load_cluster_success_t>(coordinator, std::move(p.diff));
        resources->release(resource::partial_load);
    }
}

void db_actor_t::on_commit(commit_message_t &message) noexcept {
    LOG_DEBUG(log, "closing load transaction");
    auto r = message.payload.commit();
    if (!r) {
        auto ee = make_error(r.assume_error());
        LOG_ERROR(log, "committing txn error: {}", r.assume_error().message());
        do_shutdown(ee);
    }
}

void db_actor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update", identity);
    auto &diff = *message.payload.diff;
    auto set = folder_infos_set_t{};
    auto r = diff.visit(*this, &set);
    while (r && !set.empty()) {
        r = save_folder_info(**set.begin(), &set);
    }

    if (r) {
        r = commit_on_demand();
    }
    if (!r) {
        auto ee = make_error(r.assume_error());
        LOG_ERROR(log, "on_model_update error: {}", r.assume_error().message());
        do_shutdown(ee);
    }
}

auto db_actor_t::save_folder_info(const model::folder_info_t &folder_info, void *custom) noexcept
    -> outcome::result<void> {
    auto txn_opt = get_txn();
    if (!txn_opt) {
        return txn_opt.assume_error();
    }
    auto &txn = *txn_opt.assume_value();

    auto fi_key = folder_info.get_key();
    auto fi_data = folder_info.serialize();
    auto r = db::save({fi_key, fi_data}, txn);

    if (!r) {
        return r;
    }

    auto folder_infos = reinterpret_cast<folder_infos_set_t *>(custom);
    auto it = folder_infos->find(&folder_info);
    if (it != folder_infos->end()) {
        folder_infos->erase(it);
    }
    return force_commit();
}

auto db_actor_t::remove(const model::diff::modify::generic_remove_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    for (auto &key : diff.keys) {
        auto r = db::remove(key, txn);
        if (!r) {
            return r.assume_error();
        }
    }

    auto r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::peer::cluster_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto &txn = *get_txn().assume_value();

    auto &pending = cluster->get_pending_folders();
    if (pending.size()) {
        for (auto &it : pending) {
            auto &uf = it.item;
            auto key = uf->get_key();
            auto data = uf->serialize();

            auto r = db::save({key, data}, txn);
            if (!r) {
                return r.assume_error();
            }
        }
    }

    auto r = diff.visit_next(*this, custom);
    if (r.has_error()) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto &db = diff.db;
    auto folder = cluster->get_folders().by_id(db::get_id(db));
    assert(folder);
    auto f_key = folder->get_key();
    auto f_data = folder->serialize();

    auto r = db::save({f_key, f_data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::add_blocks_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto &blocks_map = cluster->get_blocks();
    for (const auto &it : diff.blocks) {
        auto block = blocks_map.by_hash(proto::get_hash(it));
        auto key = block->get_key();
        auto data = block->serialize();
        auto r = db::save({key, data}, txn);
        if (!r) {
            return r.assume_error();
        }
    }

    auto r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::add_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto &pending = cluster->get_pending_folders();
    for (auto &item : diff.container) {
        auto &folder = db::get_folder(item.db);
        auto id = db::get_id(folder);
        auto uf = pending.by_id(id);
        if (uf && uf->device_id().get_sha256() == item.peer_id) {
            if (uf->get_id() == id) {
                auto key = uf->get_key();
                auto data = uf->serialize();

                auto r = db::save({key, data}, txn);
                if (!r) {
                    return r.assume_error();
                }
                break;
            }
        }
    }

    auto r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::add_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    auto device = cluster->get_ignored_devices().by_sha256(diff.device_id.get_sha256());
    auto key = device->get_key();
    auto data = device->serialize();

    r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::add_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();
    auto device = cluster->get_pending_devices().by_sha256(diff.device_id.get_sha256());

    auto key = device->get_key();
    auto data = device->serialize();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }
    return force_commit();
}

auto db_actor_t::operator()(const model::diff::load::remove_corrupted_files_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return remove(static_cast<const model::diff::modify::generic_remove_t &>(diff), custom);
}

auto db_actor_t::operator()(const model::diff::modify::remove_blocks_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return remove(static_cast<const model::diff::modify::generic_remove_t &>(diff), custom);
}

auto db_actor_t::operator()(const model::diff::modify::remove_files_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return remove(static_cast<const model::diff::modify::generic_remove_t &>(diff), custom);
}

auto db_actor_t::operator()(const model::diff::modify::remove_folder_infos_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return remove(static_cast<const model::diff::modify::generic_remove_t &>(diff), custom);
}

auto db_actor_t::operator()(const model::diff::modify::remove_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return remove(static_cast<const model::diff::modify::generic_remove_t &>(diff), custom);
}

auto db_actor_t::operator()(const model::diff::modify::remove_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &txn = *get_txn().assume_value();

    auto r = db::remove(diff.folder_key, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::remove_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }
    r = db::remove(diff.peer_key, txn);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::remove_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto r = db::remove(diff.device_key, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::remove_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto &txn = *get_txn().assume_value();

    auto r = db::remove(diff.device_key, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::update_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto &device_id = diff.peer_id;
    auto device = cluster->get_devices().by_sha256(device_id);
    assert(device);

    auto key = device->get_key();
    auto data = device->serialize();
    auto &txn = *get_txn().assume_value();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto device = cluster->get_devices().by_sha256(diff.device_id);
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*device);

    auto r = save_folder_info(*folder_info, custom);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return r;
}

auto db_actor_t::operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto folder = cluster->get_folders().by_id(diff.folder_id);
    if (folder && !folder->is_suspended()) {
        auto folder_info = folder->get_folder_infos().by_device(*cluster->get_device());
        if (folder_info) {
            auto name = proto::get_name(diff.proto_local);
            auto file = folder_info->get_file_infos().by_name(name);
            auto &txn = *get_txn().assume_value();

            {
                auto key = file->get_key();
                auto data = file->serialize();
                auto r = db::save({key, data}, txn);
                if (!r) {
                    return r.assume_error();
                }
            }

            auto folder_infos = reinterpret_cast<folder_infos_set_t *>(custom);
            folder_infos->emplace(folder_info.get());

            auto r = diff.visit_next(*this, custom);
            if (!r) {
                return r.assume_error();
            }
        }
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto txn_opt = get_txn();
    if (!txn_opt) {
        return txn_opt.assume_error();
    }
    auto &txn = *txn_opt.assume_value();

    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(diff.peer_id);

    auto fi_key = folder_info->get_key();
    auto fi_data = folder_info->serialize();
    auto r = db::save({fi_key, fi_data}, txn);
    if (!r) {
        return r.assume_error();
    }

    auto &files_map = folder_info->get_file_infos();
    for (const auto &f : diff.files) {
        auto name = proto::get_name(f);
        auto file = files_map.by_name(name);
        LOG_TRACE(log, "saving '{}', seq. = {}", *file, file->get_sequence());
        auto key = file->get_key();
        auto data = file->serialize();
        auto r = db::save({key, data}, txn);
        if (!r) {
            return r.assume_error();
        }
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::contact::peer_state_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    if (!diff.has_been_online) {
        return outcome::success();
    }

    auto &device_id = diff.peer_id;
    auto device = cluster->get_devices().by_sha256(device_id);
    assert(device);

    auto key = device->get_key();
    auto data = device->serialize();
    auto &txn = *get_txn().assume_value();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::contact::ignored_connected_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto device = cluster->get_ignored_devices().by_sha256(diff.device_id.get_sha256());
    assert(device);

    auto key = device->get_key();
    auto data = device->serialize();
    auto &txn = *get_txn().assume_value();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

auto db_actor_t::operator()(const model::diff::contact::unknown_connected_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto device = cluster->get_pending_devices().by_sha256(diff.device_id.get_sha256());
    assert(device);

    auto key = device->get_key();
    auto data = device->serialize();
    auto &txn = *get_txn().assume_value();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    r = diff.visit_next(*this, custom);
    if (!r) {
        return r.assume_error();
    }

    return force_commit();
}

} // namespace syncspirit::net
