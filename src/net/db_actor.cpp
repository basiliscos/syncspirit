#include "db_actor.h"
#include "names.h"
#include <cstddef>
#include <string_view>
#include "../db/prefix.h"
#include "../db/utils.h"
#include "../db/error_code.h"
#include "../model/diff/aggregate.h"
#include "../model/diff/load/blocks.h"
#include "../model/diff/load/close_transaction.h"
#include "../model/diff/load/devices.h"
#include "../model/diff/load/file_infos.h"
#include "../model/diff/load/folder_infos.h"
#include "../model/diff/load/folders.h"
#include "../model/diff/load/ignored_devices.h"
#include "../model/diff/load/ignored_folders.h"
#include "../model/diff/modify/create_folder.h"
#include "../model/diff/modify/share_folder.h"
#include "../model/diff/modify/update_peer.h"
#include "../model/diff/diff_visitor.h"

namespace syncspirit::net {

namespace {
namespace resource {
r::plugin::resource_id_t db = 0;
}

} // namespace

db_actor_t::db_actor_t(config_t &config)
    : r::actor_base_t{config}, env{nullptr}, db_dir{config.db_dir}, cluster{config.cluster} {
    log = utils::get_logger("net.db");
    auto r = mdbx_env_create(&env);
    if (r != MDBX_SUCCESS) {
        LOG_CRITICAL(log, "{}, mbdx environment creation error ({}): {}", r, mdbx_strerror(r), names::db);
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
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(names::db, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::db, get_address());
        p.discover_name(names::coordinator, coordinator, false).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&db_actor_t::on_model_update, coordinator);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        open();
        p.subscribe_actor(&db_actor_t::on_cluster_load);
    });
}

void db_actor_t::open() noexcept {
    resources->acquire(resource::db);
    auto& my_device = cluster->get_device();
    auto flags = MDBX_WRITEMAP | MDBX_COALESCE | MDBX_LIFORECLAIM | MDBX_EXCLUSIVE;
    auto r = mdbx_env_open(env, db_dir.c_str(), flags, 0664);
    if (r != MDBX_SUCCESS) {
        LOG_ERROR(log, "{}, open, mbdx open environment error ({}): {}", identity, r, mdbx_strerror(r));
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto txn = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn) {
        LOG_ERROR(log, "{}, open, cannot create transaction {}", identity, txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }

    auto db_ver = db::get_version(txn.value());
    if (!db_ver) {
        LOG_ERROR(log, "{}, open, cannot get db version :: {}", identity, db_ver.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto version = db_ver.value();
    LOG_DEBUG(log, "got db version: {}, expected : {} ", version, db::version);


    if (!txn) {
        LOG_ERROR(log, "{}, open, cannot create transaction {}", identity, txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    if (db_ver.value() != db::version) {
        txn = db::make_transaction(db::transaction_type_t::RW, txn.value());
        auto r = db::migrate(version, my_device, txn.value());
        if (!r) {
            LOG_ERROR(log, "{}, open, cannot migrate db {}", identity, r.error().message());
            resources->release(resource::db);
            return do_shutdown(make_error(r.error()));
        }
        LOG_INFO(log, "{}, open, successufully migrated db: {} -> {} ", identity, version, db::version);
    }
    resources->release(resource::db);
}

void db_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    LOG_TRACE(log, "{}, on_start", identity);
}

void db_actor_t::on_cluster_load(message::load_cluster_request_t &request) noexcept {
    using namespace model::diff;
    using container_t = aggregate_t::diffs_t;

    auto txn_opt = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn_opt) {
        return reply_with_error(request, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();

    auto devices_opt = db::load(db::prefix::device, txn);
    if (!devices_opt) {
        return reply_with_error(request, make_error(devices_opt.error()));
    }

    auto blocks_opt = db::load(db::prefix::block_info, txn);
    if (!blocks_opt) {
        return reply_with_error(request, make_error(blocks_opt.error()));
    }

    auto ignored_devices_opt = db::load(db::prefix::ignored_device, txn);
    if (!ignored_devices_opt) {
        return reply_with_error(request, make_error(ignored_devices_opt.error()));
    }

    auto ignored_folders_opt = db::load(db::prefix::ignored_folder, txn);
    if (!ignored_folders_opt) {
        return reply_with_error(request, make_error(ignored_folders_opt.error()));
    }

    auto folders_opt = db::load(db::prefix::folder, txn);
    if (!folders_opt) {
        return reply_with_error(request, make_error(folders_opt.error()));
    }

    auto folder_infos_opt = db::load(db::prefix::folder_info, txn);
    if (!folder_infos_opt) {
        return reply_with_error(request, make_error(folder_infos_opt.error()));
    }

    auto file_infos_opt = db::load(db::prefix::file_info, txn);
    if (!file_infos_opt) {
        return reply_with_error(request, make_error(file_infos_opt.error()));
    }

    container_t container;
    container.emplace_back(new load::devices_t(std::move(devices_opt.value())));
    container.emplace_back(new load::blocks_t(std::move(blocks_opt.value())));
    container.emplace_back(new load::ignored_devices_t(std::move(ignored_devices_opt.value())));
    container.emplace_back(new load::ignored_folders_t(std::move(ignored_folders_opt.value())));
    container.emplace_back(new load::folders_t(std::move(folders_opt.value())));
    container.emplace_back(new load::folder_infos_t(std::move(folder_infos_opt.value())));
    container.emplace_back(new load::file_infos_t(std::move(file_infos_opt.value())));
    container.emplace_back(new load::close_transaction_t(std::move(txn)));

    cluster_diff_ptr_t r = cluster_diff_ptr_t(new aggregate_t(std::move(container)));

    reply_to(request, r);
}

void db_actor_t::on_model_update(message::model_update_t &message) noexcept {
    LOG_TRACE(log, "{}, on_model_update", identity);
    auto& diff = *message.payload.diff;
    auto r = diff.visit(*this);
    if (!r) {
        auto ee = make_error(r.assume_error());
        do_shutdown(ee);
    }
}


auto db_actor_t::operator()(const model::diff::modify::create_folder_t &diff) noexcept -> outcome::result<void>  {
    if (cluster->is_tainted()) {
        return outcome::success();
    }
    auto& folder_id = diff.item.id();
    auto folder = cluster->get_folders().by_id(folder_id);
    assert(folder);
    auto f_key = folder->get_key();
    auto f_data = folder->serialize();

    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return txn_opt.assume_error();
    }
    auto &txn = txn_opt.value();

    auto r = db::save({f_key, f_data}, txn);
    if (!r) {
        return r.assume_error();
    }

    auto folder_info = folder->get_folder_infos().by_device(cluster->get_device());
    auto fi_key = folder_info->get_key();
    auto fi_data = folder_info->serialize();
    r = db::save({fi_key, fi_data}, txn);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success();
}

auto db_actor_t::operator()(const model::diff::modify::share_folder_t &diff) noexcept -> outcome::result<void>  {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto peer = cluster->get_devices().by_sha256(diff.peer_id);
    assert(peer);
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    assert(folder);

    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return txn_opt.assume_error();
    }
    auto &txn = txn_opt.value();

    auto folder_info = folder->get_folder_infos().by_device(peer);
    assert(folder_info);

    auto fi_key = folder_info->get_key();
    auto fi_data = folder_info->serialize();
    auto r = db::save({fi_key, fi_data}, txn);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success();
}

auto db_actor_t::operator()(const model::diff::modify::update_peer_t &diff) noexcept -> outcome::result<void>  {
    if (cluster->is_tainted()) {
        return outcome::success();
    }

    auto& device_id = diff.peer_id;
    auto device = cluster->get_devices().by_sha256(device_id);
    assert(device);

    auto key = device->get_key();
    auto data = device->serialize();

    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return txn_opt.assume_error();
    }
    auto &txn = txn_opt.value();

    auto r = db::save({key, data}, txn);
    if (!r) {
        return r.assume_error();
    }

    return outcome::success();
}


#if 0
void db_actor_t::on_store_ignored_device(message::store_ignored_device_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto &peer_device = message.payload.request_payload.device;
    auto r = db::store_ignored_device(peer_device, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    reply_to(message);
}

void db_actor_t::on_store_device(message::store_device_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto &peer_device = message.payload.request_payload.device;
    auto r = db::store_device(peer_device, txn);
    if (!r) {
        reply_with_error(message, make_error(r.assume_error()));
        return;
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    reply_to(message);
}

void db_actor_t::on_store_ignored_folder(message::store_ignored_folder_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto &folder = message.payload.request_payload.folder;
    auto r = db::store_ignored_folder(folder, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    reply_to(message);
}

void db_actor_t::on_store_new_folder(message::store_new_folder_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto &db_folder = message.payload.request_payload.folder;
    auto folder = model::folder_ptr_t(new model::folder_t(db_folder));

    auto r = db::store_folder(folder, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    // local
    auto key_option = txn.next_sequence();
    if (!key_option) {
        reply_with_error(message, make_error(key_option.error()));
        return;
    }
    auto key_local = key_option.value();
    db::FolderInfo db_fi_local;
    db_fi_local.set_index_id(distribution(generator));
    db_fi_local.set_max_sequence(0);
    auto src_local = device.get();
    auto fi_local = model::folder_info_ptr_t(new model::folder_info_t(db_fi_local, src_local, folder.get(), key_local));

    r = db::store_folder_info(fi_local, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    // remote
    auto fi_source = model::folder_info_ptr_t{};
    auto &src_index = message.payload.request_payload.source_index;
    auto &source = message.payload.request_payload.source;
    if (src_index) {
        assert(source);
        db::FolderInfo db_fi_source;
        db_fi_source.set_index_id(src_index);
        db_fi_source.set_max_sequence(0); // always start afresh
        auto src = message.payload.request_payload.source.get();

        key_option = txn.next_sequence();
        if (!key_option) {
            reply_with_error(message, make_error(key_option.error()));
            return;
        }
        auto key_source = key_option.value();
        fi_source = model::folder_info_ptr_t(new model::folder_info_t(db_fi_source, src, folder.get(), key_source));

        r = db::store_folder_info(fi_source, txn);
        if (!r) {
            reply_with_error(message, make_error(r.error()));
            return;
        }
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    folder->add(fi_local);
    if (fi_source) {
        folder->add(fi_source);
    }

    auto &cluster = message.payload.request_payload.cluster;
    auto &folders = cluster->get_folders();
    folders.put(folder);
    folder->assign_cluster(cluster.get());
    folder->assign_device(device);

    reply_to(message, std::move(folder));
}

void db_actor_t::on_store_folder(message::store_folder_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto &folder = message.payload.request_payload.folder;
    auto r = db::store_folder(folder, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }
    folder->unmark_dirty();

    for (auto &it : folder->get_folder_infos()) {
        auto &fi = it.second;
        if (!fi->is_dirty()) {
            continue;
        }
        r = db::store_folder_info(fi, txn);
        LOG_TRACE(log, "{}, on_store_folder folder_info = {} max seq = {}", identity, fi->get_db_key(),
                  fi->get_max_sequence());
        if (!r) {
            reply_with_error(message, make_error(r.error()));
            return;
        }
        fi->unmark_dirty();
        folder->add(fi);
    }

    for (auto &it : folder->get_folder_infos()) {
        auto &fi = it.second;
        if (!fi->is_dirty()) {
            continue;
        }
        auto r = save(txn, fi);
        if (!r) {
            reply_with_error(message, make_error(r.error()));
            return;
        }
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    reply_to(message);
}

outcome::result<void> db_actor_t::save(db::transaction_t &txn, model::folder_info_ptr_t &folder_info) noexcept {
    auto r = db::store_folder_info(folder_info, txn);
    if (!r) {
        return r;
    }

    auto &folder = *folder_info->get_folder();
    auto &cluster = *folder.get_cluster();
    auto &blocks_map = cluster.get_blocks();
    auto file_infos = model::file_infos_map_t();
    for (auto &it : folder_info->get_file_infos()) {
        auto &fi = it.second;
        if (!fi->is_dirty()) {
            continue;
        }

        for (auto &block : fi->get_blocks()) {
            if (!block->is_dirty()) {
                continue;
            }
            r = db::store_block_info(block, txn);
            if (!r) {
                return r;
            }
            block->unmark_dirty();
            blocks_map.put(block);
        }

        r = db::store_file_info(it.second, txn);
        if (!r) {
            return r.assume_error();
        }
        fi->unmark_dirty();
        file_infos.put(fi);
    }

    auto &native_file_infos = folder_info->get_file_infos();
    for (auto it : file_infos) {
        native_file_infos.put(it.second);
    }

    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    r = db::remove(deleted_blocks_map, txn);
    if (!r) {
        return r;
    }
    deleted_blocks_map.clear();

    folder.get_folder_infos().put(folder_info);
    return outcome::success();
}

void db_actor_t::on_store_folder_info(message::store_folder_info_request_t &message) noexcept {
    auto &fi = message.payload.request_payload.folder_info;
    LOG_TRACE(log, "{}, on_store_folder_info folder_info = {}", identity, fi->get_db_key());
    if (!fi->is_dirty()) {
        LOG_WARN(log, "{}, folder_info = {}  (from {}) is not dirty, no need to save", identity, fi->get_db_key(),
                 fi->get_folder()->label());
        reply_to(message);
        return;
    }

    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();
    auto r = save(txn, fi);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    fi->unmark_dirty();
    reply_to(message);
}

void db_actor_t::on_store_file(message::store_file_request_t &message) noexcept {
    auto &file = message.payload.request_payload.file;
    LOG_TRACE(log, "{}, on_store_file file = {}", identity, file->get_full_name());
    assert(file->is_dirty() && "file should be marked dirty");

    auto txn_opt = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();

    auto r = db::store_file_info(file, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }
    file->unmark_dirty();

    auto folder_info = file->get_folder_info();
    if (folder_info->is_dirty()) {
        auto fi = model::folder_info_ptr_t(folder_info);
        r = db::store_folder_info(fi, txn);
        if (!r) {
            reply_with_error(message, make_error(r.error()));
            return;
        }
        fi->unmark_dirty();
    }

    auto &folder = *folder_info->get_folder();
    auto &cluster = *folder.get_cluster();
    auto &deleted_blocks_map = cluster.get_deleted_blocks();
    r = db::remove(deleted_blocks_map, txn);
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    r = txn.commit();
    if (!r) {
        reply_with_error(message, make_error(r.error()));
        return;
    }

    deleted_blocks_map.clear();

    auto &map = folder_info->get_file_infos();
    map.put(file);

    reply_to(message);
}
#endif

} // namespace syncspirit::net
