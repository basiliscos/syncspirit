#include "utils.h"
#include "transaction.h"
#include "error_code.h"
#include "prefix.h"
#include <boost/endian/conversion.hpp>
#include <spdlog.h>

namespace syncspirit::db {

std::uint32_t version{1};

namespace be = boost::endian;

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

static outcome::result<void> migrate0(transaction_t &txn) noexcept {
    auto key = prefixer_t<prefix::misc>::make(misc::db_version);
    MDBX_val value;
    auto db_ver = be::native_to_big(version);
    value.iov_base = &db_ver;
    value.iov_len = sizeof(db_ver);
    auto r = mdbx_put(txn.txn, txn.dbi, key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

static outcome::result<void> do_migrate(uint32_t from, transaction_t &txn) noexcept {
    switch (from) {
    case 0:
        return migrate0(txn);
    default:
        assert(0 && "impossibe migration to future version");
        std::terminate();
    }
}

outcome::result<void> migrate(uint32_t from, transaction_t &txn) noexcept {
    while (from != version) {
        auto r = do_migrate(from, txn);
        if (!r)
            return r;
        r = txn.commit();
        if (!r)
            return r;
        ++from;
    }
    return outcome::success();
}

outcome::result<void> update_folder_info(const proto::Folder &folder, transaction_t &txn) noexcept {
    auto key = prefixer_t<prefix::folder_info>::make(folder.id());
    auto data = folder.SerializeAsString();
    MDBX_val value;
    value.iov_base = data.data();
    value.iov_len = data.size();
    auto r = mdbx_put(txn.txn, txn.dbi, key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

outcome::result<void> create_folder(const proto::Folder &folder, const model::index_id_t &index_id,
                                    const model::device_id_t &device_id, transaction_t &txn) noexcept {
    auto key_index = prefixer_t<prefix::folder_index>::make(folder.id());
    auto data_index = be::native_to_big(index_id);
    MDBX_val value;
    value.iov_base = &data_index;
    value.iov_len = sizeof(data_index);
    auto r = mdbx_put(txn.txn, txn.dbi, key_index, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }

    auto key_device = prefixer_t<prefix::folder_local_device>::make(folder.id());
    value.iov_base = (void *)device_id.get_value().data();
    value.iov_len = device_id.get_value().length();
    r = mdbx_put(txn.txn, txn.dbi, key_device, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }

    return outcome::success();
}

outcome::result<model::folder_ptr_t> load_folder(config::folder_config_t &folder_cfg,
                                                 const model::devices_map_t &devices, transaction_t &txn) noexcept {
    model::folder_ptr_t folder(new model::folder_t(folder_cfg));
    auto key_info = prefixer_t<prefix::folder_info>::make(folder->id);

    MDBX_val value;
    auto r = mdbx_get(txn.txn, txn.dbi, key_info, &value);
    if (r != MDBX_SUCCESS) {
        if (r == MDBX_NOTFOUND) {
            return make_error_code(error_code::folder_info_not_found);
        }
        return make_error_code(r);
    }

    proto::Folder folder_data;
    if (!folder_data.ParseFromArray(value.iov_base, value.iov_len)) {
        return make_error_code(error_code::folder_info_deserialization_failure_t);
    }
    folder->assign(folder_data, devices);

    auto key_device = prefixer_t<prefix::folder_local_device>::make(folder->id);
    r = mdbx_get(txn.txn, txn.dbi, key_device, &value);
    if (r != MDBX_SUCCESS) {
        if (r == MDBX_NOTFOUND) {
            return make_error_code(error_code::folder_local_device_not_found);
        }
        return make_error_code(r);
    }
    std::string device_id((char *)value.iov_base, value.iov_len);
    auto it = devices.find(device_id);
    if (it == devices.end()) {
        return make_error_code(error_code::unknown_local_device);
    }
    auto &device = it->second;

    auto key_index = prefixer_t<prefix::folder_index>::make(folder->id);
    r = mdbx_get(txn.txn, txn.dbi, key_device, &value);
    if (r != MDBX_SUCCESS) {
        if (r == MDBX_NOTFOUND) {
            return make_error_code(error_code::folder_index_not_found);
        }
        return make_error_code(r);
    }
    if (value.iov_len != sizeof(model::index_id_t)) {
        return make_error_code(error_code::folder_info_deserialization_failure_t);
    }
    auto index = be::big_to_native(*((model::index_id_t *)value.iov_base));
    auto sequence = model::sequence_id_t{0};

    folder->devices.emplace(model::folder_device_t{device, index, sequence});
    return folder;
}

} // namespace syncspirit::db
