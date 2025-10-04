// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "utils.h"
#include "transaction.h"
#include "error_code.h"
#include "prefix.h"
#include "model/misc/error_code.h"
#include "proto/proto-helpers-db.h"
#include "proto/proto-helpers-impl.hpp"
#include <boost/endian/conversion.hpp>
#include <type_traits>

namespace syncspirit::db {

namespace be = boost::endian;

std::byte zero{0};
std::uint32_t version{3};

namespace misc {
static const constexpr std::string_view db_version = "db_version";
}

using migration_fn_t = outcome::result<void>(model::device_ptr_t &device, transaction_t &txn) noexcept;
using migration_fn_ptr_t = std::add_pointer_t<migration_fn_t>;

outcome::result<uint32_t> get_version(transaction_t &txn) noexcept {
    auto key = prefixer_t<prefix::misc>::make(misc::db_version);
    MDBX_val value;
    auto r = mdbx_get(txn.txn, txn.dbi, key, &value);
    if (r != MDBX_SUCCESS) {
        if (r == MDBX_NOTFOUND) {
            return outcome::success(0);
        }
        return make_error_code(r);
    }

    if (value.iov_len != sizeof(std::uint32_t)) {
        return make_error_code(error_code::db_version_size_mismatch);
    }

    std::uint32_t version;
    memcpy(&version, value.iov_base, sizeof(std::uint32_t));
    be::big_to_native_inplace(version);
    return version;
}

outcome::result<void> save_version(std::uint32_t v, transaction_t &txn) noexcept {
    using prefixes_t = std::vector<discr_t>;
    auto key = prefixer_t<prefix::misc>::make(misc::db_version);
    MDBX_val value;
    auto db_ver = be::native_to_big(v);
    value.iov_base = &db_ver;
    value.iov_len = sizeof(db_ver);
    auto r = mdbx_put(txn.txn, txn.dbi, key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

static outcome::result<void> migrate_0(model::device_ptr_t &device, transaction_t &txn) noexcept {
    auto version_r = save_version(1, txn);
    if (!version_r) {
        return version_r;
    }

    // make anchors
    using prefixes_t = std::vector<discr_t>;
    prefixes_t prefixes{prefix::device,         prefix::folder,         prefix::folder_info,
                        prefix::file_info,      prefix::ignored_device, prefix::ignored_folder,
                        prefix::pending_folder, prefix::block_info,     prefix::pending_device};
    for (auto &prefix : prefixes) {
        MDBX_val key, value;
        key.iov_base = &prefix;
        key.iov_len = sizeof(prefix);
        value.iov_base = &zero;
        value.iov_len = sizeof(zero);
        auto r = mdbx_put(txn.txn, txn.dbi, &key, &value, MDBX_UPSERT);
        if (r != MDBX_SUCCESS) {
            return make_error_code(r);
        }
    }

    auto device_key = device->get_key();
    auto device_data = device->serialize();

    MDBX_val device_db_key;
    device_db_key.iov_base = (void *)device_key.data();
    device_db_key.iov_len = device_key.size();

    MDBX_val device_db_value;
    device_db_value.iov_base = device_data.data();
    device_db_value.iov_len = device_data.size();

    auto r = mdbx_put(txn.txn, txn.dbi, &device_db_key, &device_db_value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

static outcome::result<void> migrate_1(model::device_ptr_t &device, transaction_t &txn) noexcept {
    auto fis_opt = db::load(db::prefix::folder_info, txn);
    if (!fis_opt) {
        return fis_opt.error();
    }

    auto &container = fis_opt.value();
    auto self_key = device->device_id().get_key();

    for (auto &pair : container) {
        auto &key = pair.key;

        auto db_fi = db::FolderInfo();
        if (auto left = db::decode(pair.value, db_fi); left) {
            using ec_t = model::error_code_t;
            return make_error_code(ec_t::folder_info_deserialization_failure);
        }
        db::set_introducer_device_key(db_fi, self_key);
        auto new_value = db::encode(db_fi);
        auto r = db::save({key, new_value}, txn);
        if (!r) {
            return r;
        }
    }
    return outcome::success();
}

static outcome::result<void> migrate_2(model::device_ptr_t &device, transaction_t &txn) noexcept {
    // clang-format off
    using BlockInfoEx = pp::message<
       pp::uint32_field <"weak_hash", 1>,
       pp::int32_field  <"size",      2>
    >;
    // clang-format on

    auto bis_opt = db::load(db::prefix::block_info, txn);
    if (!bis_opt) {
        return bis_opt.error();
    }

    auto &container = bis_opt.value();

    for (auto &pair : container) {
        using namespace pp;
        auto &key = pair.key;
        auto db_bi_ex = BlockInfoEx();
        if (auto left = syncspirit::details::generic_decode(pair.value, db_bi_ex); left) {
            continue;
        }
        auto size = db_bi_ex["size"_f].value_or(0);
        auto db_bi = db::BlockInfo();
        db::set_size(db_bi, size);
        auto new_value = db::encode(db_bi);
        auto r = db::save({key, new_value}, txn);
        if (!r) {
            return r;
        }
    }

    return save_version(db::version, txn);
}

static outcome::result<void> do_migrate(uint32_t from, model::device_ptr_t &device, transaction_t &txn) noexcept {
    static constexpr uint32_t SZ = 3;
    migration_fn_ptr_t migrations[SZ] = {
        migrate_0,
        migrate_1,
        migrate_2,
    };

    if (from >= SZ) {
        return db::make_error_code(error_code::cannot_downgrade_db);
    }
    return migrations[from](device, txn);
}

outcome::result<void> migrate(uint32_t from, model::device_ptr_t device, transaction_t &txn) noexcept {
    while (from != version) {
        auto r = do_migrate(from, device, txn);
        if (!r)
            return r;
        if (!r)
            return r;
        ++from;
    }
    return txn.commit();
}

outcome::result<container_t> load(discr_t prefix, transaction_t &txn) noexcept {
    auto cursor_opt = txn.cursor();
    if (!cursor_opt) {
        return cursor_opt.error();
    }
    auto &cursor = cursor_opt.value();
    container_t container;
    auto r = cursor.iterate(prefix, [&](auto key, auto value) -> outcome::result<void> {
        container.push_back(pair_t{key, value});
        return outcome::success();
    });
    if (!r) {
        return r.error();
    }
    return outcome::success(std::move(container));
}

outcome::result<void> save(const pair_t &container, transaction_t &txn) noexcept {
    MDBX_val key;
    key.iov_base = (void *)container.key.data();
    key.iov_len = container.key.size();

    MDBX_val value;
    value.iov_base = (void *)container.value.data();
    value.iov_len = container.value.size();

    auto r = mdbx_put(txn.txn, txn.dbi, &key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

outcome::result<void> remove(utils::bytes_view_t key_, transaction_t &txn) noexcept {
    MDBX_val key;
    key.iov_base = (void *)key_.data();
    key.iov_len = key_.size();

    auto r = mdbx_del(txn.txn, txn.dbi, &key, nullptr);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

} // namespace syncspirit::db
