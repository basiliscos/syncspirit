#include "db_actor.h"
#include "names.h"
#include "spdlog/spdlog.h"
#include <cstddef>
#include <string_view>
#include "../db/utils.h"
#include "../db/error_code.h"
#include "../db/transaction.h"

namespace syncspirit::net {

namespace {
namespace resource {
r::plugin::resource_id_t db = 0;
}

} // namespace

db_actor_t::db_actor_t(config_t &config)
    : r::actor_base_t{config}, env{nullptr}, db_dir{config.db_dir}, device{config.device} {
    auto r = mdbx_env_create(&env);
    if (r != MDBX_SUCCESS) {
        spdlog::critical("{}, mbdx environment creation error ({}): {}", r, mdbx_strerror(r), names::db);
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
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) { p.register_name(names::db, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        open();
        p.subscribe_actor(&db_actor_t::on_cluster_load);
        p.subscribe_actor(&db_actor_t::on_store_ingnored_device);
        p.subscribe_actor(&db_actor_t::on_store_device);
    });
}

void db_actor_t::open() noexcept {
    resources->acquire(resource::db);
    auto flags = MDBX_WRITEMAP | MDBX_NOTLS | MDBX_COALESCE | MDBX_LIFORECLAIM;
    auto r = mdbx_env_open(env, db_dir.c_str(), flags, 0664);
    if (r != MDBX_SUCCESS) {
        spdlog::error("{}, open, mbdx open environment error ({}): {}", identity, r, mdbx_strerror(r));
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto txn = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn) {
        spdlog::error("{}, open, cannot create transaction {}", identity, txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }

    auto db_ver = db::get_version(txn.value());
    if (!db_ver) {
        spdlog::error("{}, open, cannot get db version :: {}", identity, db_ver.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    auto version = db_ver.value();
    spdlog::debug("got db version: {}, expected : {} ", version, db::version);

    txn = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn) {
        spdlog::error("{}, open, cannot create transaction {}", identity, txn.error().message());
        resources->release(resource::db);
        return do_shutdown(make_error(db::make_error_code(r)));
    }
    if (db_ver.value() != db::version) {
        auto r = db::migrate(version, txn.value());
        if (!r) {
            spdlog::error("{}, open, cannot migrate db {}", identity, r.error().message());
            resources->release(resource::db);
            return do_shutdown(make_error(r.error()));
        }
        spdlog::info("{}, open, successufully migrated db: {} -> {} ", identity, version, db::version);
    }
    resources->release(resource::db);
}

void db_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("{}, on_start", identity);
}

#if 0
void db_actor_t::on_make_index_id(message::make_index_id_request_t &message) noexcept {
    auto txn = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn) {
        return reply_with_error(message, make_error(txn.error()));
    }
    auto &payload = message.payload.request_payload;
    auto &orig = payload->folder;
    model::index_id_t index_id{distribution(rd)};
    auto r = db::update_folder_info(orig, txn.value());
    if (!r) {
        return reply_with_error(message, make_error(r.error()));
    }
    r = db::create_folder(orig, index_id, device->device_id, txn.value());
    if (!r) {
        return reply_with_error(message, make_error(r.error()));
    }
    r = txn.value().commit();
    if (!r) {
        return reply_with_error(message, make_error(r.error()));
    }
    spdlog::trace("{}, on_make_index_id, created index = {} for folder = {}", identity, index_id, orig.label());
    reply_to(message, index_id);
}
#endif

void db_actor_t::on_cluster_load(message::load_cluster_request_t &message) noexcept {
    auto txn_opt = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn_opt) {
        return reply_with_error(message, make_error(txn_opt.error()));
    }
    auto &txn = txn_opt.value();

    auto devices_opt = db::load_devices(txn);
    if (!devices_opt) {
        return reply_with_error(message, make_error(devices_opt.error()));
    }
    auto &devices = devices_opt.value();

    auto ignored_devices_opt = db::load_ignored_devices(txn);
    if (!ignored_devices_opt) {
        return reply_with_error(message, make_error(ignored_devices_opt.error()));
    }
    auto &ignored_devices = ignored_devices_opt.value();

    auto folders_opt = db::load_folders(txn_opt.value());
    if (!folders_opt) {
        return reply_with_error(message, make_error(folders_opt.error()));
    }
    auto &folders = folders_opt.value();

    auto folder_infos_opt = db::load_folder_infos(devices, folders, txn);
    if (!folder_infos_opt) {
        return reply_with_error(message, make_error(folder_infos_opt.error()));
    }
    auto folder_infos = folder_infos_opt.value();

    auto file_infos_opt = db::load_file_infos(folder_infos, txn);
    if (!file_infos_opt) {
        return reply_with_error(message, make_error(file_infos_opt.error()));
    }
    auto &file_infos = file_infos_opt.value();

    // correctly link
    for (auto &it : file_infos) {
        auto &fi = it.second;
        fi->get_folder_info()->add(fi);
    }

    for (auto &it : folder_infos) {
        auto &fi = it.second;
        fi->get_folder()->add(fi);
    }

    if (auto my_d = devices.by_id(device->get_id()); !my_d) {
        devices.put(device);
    }

    auto cluster = model::cluster_ptr_t(new model::cluster_t(device));
    cluster->assign_folders(std::move(folders));
    reply_to(message, std::move(cluster), std::move(devices), std::move(ignored_devices));
}

void db_actor_t::on_store_ingnored_device(message::store_ignored_device_request_t &message) noexcept {
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

} // namespace syncspirit::net
