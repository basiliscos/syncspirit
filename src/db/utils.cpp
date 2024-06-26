// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "utils.h"
#include "transaction.h"
#include "error_code.h"
#include "prefix.h"
#include <boost/endian/conversion.hpp>

namespace syncspirit::db {

namespace be = boost::endian;

std::byte zero{0};
std::uint32_t version{1};

namespace misc {
static const constexpr std::string_view db_version = "db_version";
}

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

static outcome::result<void> migrate0(model::device_ptr_t &device, transaction_t &txn) noexcept {
    using prefixes_t = std::vector<discr_t>;
    auto key = prefixer_t<prefix::misc>::make(misc::db_version);
    MDBX_val value;
    auto db_ver = be::native_to_big(version);
    value.iov_base = &db_ver;
    value.iov_len = sizeof(db_ver);
    auto r = mdbx_put(txn.txn, txn.dbi, key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }

    // make anchors
    prefixes_t prefixes{prefix::device,         prefix::folder,         prefix::folder_info,
                        prefix::file_info,      prefix::ignored_device, prefix::ignored_folder,
                        prefix::unknown_folder, prefix::block_info,     prefix::unknown_device};
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

    r = mdbx_put(txn.txn, txn.dbi, &device_db_key, &device_db_value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

static outcome::result<void> do_migrate(uint32_t from, model::device_ptr_t &device, transaction_t &txn) noexcept {
    switch (from) {
    case 0:
        return migrate0(device, txn);
    default:
        assert(0 && "impossibe migration to future version");
        std::terminate();
    }
}

outcome::result<void> migrate(uint32_t from, model::device_ptr_t device, transaction_t &txn) noexcept {
    while (from != version) {
        auto r = do_migrate(from, device, txn);
        if (!r)
            return r;
        r = txn.commit();
        if (!r)
            return r;
        ++from;
    }
    return outcome::success();
}

outcome::result<container_t> load(discr_t prefix, transaction_t &txn) noexcept {
    char prefix_val = (char)prefix;
    std::string_view prefix_mask(&prefix_val, 1);
    auto cursor_opt = txn.cursor();
    if (!cursor_opt) {
        return cursor_opt.error();
    }
    auto &cursor = cursor_opt.value();
    container_t container;
    auto r = cursor.iterate(prefix_mask, [&](auto &key, auto &value) -> outcome::result<void> {
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

outcome::result<void> remove(std::string_view key_, transaction_t &txn) noexcept {
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
