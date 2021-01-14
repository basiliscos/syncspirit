#include "db_actor.h"
#include "names.h"
#include "spdlog/spdlog.h"
#include <cstddef>
#include <string_view>
#include "../db/utils.h"
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
        spdlog::critical("db_actor_t::db_actor_t, mbdx environment creation error ({}): {}", r, mdbx_strerror(r));
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
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.register_name(names::db, get_address());
        // p.discover_name(names::coordinator, coordinator, false).link();
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        open();
        p.subscribe_actor(&db_actor_t::on_make_index_id);
        p.subscribe_actor(&db_actor_t::on_load_folder);
    });
}

void db_actor_t::open() noexcept {
    resources->acquire(resource::db);
    auto flags = MDBX_WRITEMAP | MDBX_NOTLS | MDBX_COALESCE | MDBX_LIFORECLAIM;
    auto r = mdbx_env_open(env, db_dir.c_str(), flags, 0664);
    if (r != MDBX_SUCCESS) {
        spdlog::error("db_actor_t::open, mbdx open environment error ({}): {}", r, mdbx_strerror(r));
        resources->release(resource::db);
        return do_shutdown();
    }
    auto txn = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn) {
        spdlog::error("db_actor_t::open, cannot create transaction {}", txn.error().message());
        resources->release(resource::db);
        return do_shutdown();
    }

    auto db_ver = db::get_version(txn.value());
    if (!db_ver) {
        spdlog::error("db_actor_t::open, cannot get db version :: {}", db_ver.error().message());
        resources->release(resource::db);
        return do_shutdown();
    }
    auto version = db_ver.value();
    spdlog::debug("got db version: {}, expected : {} ", version, db::version);

    txn = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn) {
        spdlog::error("db_actor_t::open, cannot create transaction {}", txn.error().message());
        resources->release(resource::db);
        return do_shutdown();
    }
    if (db_ver.value() != db::version) {
        auto r = db::migrate(version, txn.value());
        if (!r) {
            spdlog::error("db_actor_t::open, cannot migrate db {}", r.error().message());
            resources->release(resource::db);
            return do_shutdown();
        }
        spdlog::info("db_actor_t::open, successufully migrated db: {} -> {} ", version, db::version);
    }
    resources->release(resource::db);
}

void db_actor_t::on_start() noexcept {
    r::actor_base_t::on_start();
    spdlog::trace("db_actor_t::on_start (addr = {})", (void *)address.get());
}

void db_actor_t::on_make_index_id(message::make_index_id_request_t &message) noexcept {
    auto txn = db::make_transaction(db::transaction_type_t::RW, env);
    if (!txn) {
        return reply_with_error(message, txn.error());
    }
    auto &payload = message.payload.request_payload;
    auto &orig = payload.folder;
    model::index_id_t index_id{distribution(rd)};
    auto r = db::update_folder_info(orig, txn.value());
    if (!r) {
        return reply_with_error(message, r.error());
    }
    r = db::create_folder(orig, index_id, device->device_id, txn.value());
    if (!r) {
        return reply_with_error(message, r.error());
    }
    r = txn.value().commit();
    if (!r) {
        return reply_with_error(message, r.error());
    }
    spdlog::trace("db_actor_t::on_make_index_id, created index = {} for folder = {}", index_id, orig.label());
    reply_to(message, index_id);
}

void db_actor_t::on_load_folder(message::load_folder_request_t &message) noexcept {
    auto txn = db::make_transaction(db::transaction_type_t::RO, env);
    if (!txn) {
        return reply_with_error(message, txn.error());
    }
    auto &p = message.payload.request_payload;
    auto r = db::load_folder(p.folder, *p.devices, txn.value());
    if (!r) {
        reply_with_error(message, r.error());
    } else {
        reply_to(message, r.value());
    }
}

} // namespace syncspirit::net
