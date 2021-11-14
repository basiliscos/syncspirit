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
    prefixes_t prefixes{prefix::device,         prefix::folder,         prefix::folder_info, prefix::file_info,
                        prefix::ignored_device, prefix::ignored_folder, prefix::block_info};
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
    device_db_key.iov_base = (void*) device_key.data();
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

outcome::result<void> save(const pair_t& container, transaction_t &txn) noexcept {
    MDBX_val key;
    key.iov_base = (void*)container.key.data();
    key.iov_len = container.key.size();

    MDBX_val value;
    value.iov_base = (void*)container.value.data();
    value.iov_len = container.value.size();

    auto r = mdbx_put(txn.txn, txn.dbi, &key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

outcome::result<void> remove(std::string_view key_, transaction_t &txn) noexcept {
    MDBX_val key;
    key.iov_base = (void*)key_.data();
    key.iov_len = key_.size();

    auto r = mdbx_del(txn.txn, txn.dbi, &key, nullptr);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}


#if 0

using ignored_device_t = model::device_id_t;


template <typename T> struct persister_helper_t;

template <> struct persister_helper_t<model::device_t> {
    static const constexpr discr_t prefix = prefix::device;
    static const constexpr bool need_synt_key{true};
    using type = model::device_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::devices_map_t;
    using db_type = db::Device;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_db_key()); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(item); }

    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <> struct persister_helper_t<model::folder_t> {
    static const constexpr discr_t prefix = prefix::folder;
    static const constexpr bool need_synt_key{true};
    using type = model::folder_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::folders_map_t;
    using db_type = db::Folder;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_db_key()); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(item); }

    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <> struct persister_helper_t<model::folder_info_t> {
    static const constexpr discr_t prefix = prefix::folder_info;
    static const constexpr bool need_synt_key{true};
    using type = model::folder_info_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::folder_infos_map_t;
    using db_type = db::FolderInfo;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_db_key()); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(std::move(item)); }
    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <> struct persister_helper_t<model::file_info_t> {
    static const constexpr discr_t prefix = prefix::file_info;
    static const constexpr bool need_synt_key{false};
    using type = model::file_info_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::file_infos_map_t;
    using db_type = db::FileInfo;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_db_key()); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(std::move(item)); }
    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <> struct persister_helper_t<ignored_device_t> {
    static const constexpr discr_t prefix = prefix::ignored_device;
    static const constexpr bool need_synt_key{false};
    using type = model::device_id_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::ignored_devices_map_t;
    using db_type = void;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_sha256()); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(std::move(item)); }
    static std::string serialize(const ptr_t &) noexcept {
        return std::string(reinterpret_cast<const char *>(&zero), 1);
    }
};

template <> struct persister_helper_t<model::ignored_folder_t> {
    static const constexpr discr_t prefix = prefix::ignored_folder;
    static const constexpr bool need_synt_key{false};
    using type = model::ignored_folder_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::ignored_folders_map_t;
    using db_type = db::IgnoredFolder;

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_key); }

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(std::move(item)); }

    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <> struct persister_helper_t<model::block_info_t> {
    static const constexpr discr_t prefix = prefix::block_info;
    static const constexpr bool need_synt_key{true};
    using type = model::block_info_t;
    using my_prefix = prefixer_t<prefix>;
    using ptr_t = model::intrusive_ptr_t<type>;
    using collection_t = model::block_infos_map_t;
    using db_type = db::BlockInfo;

    static void append(collection_t &dest, ptr_t &&item) noexcept { dest.put(std::move(item)); }

    static value_t get_db_key(const ptr_t &item) noexcept { return my_prefix::make(item->get_db_key()); }

    static auto serialize(const ptr_t &item) noexcept { return item->serialize().SerializeAsString(); }
};

template <typename T, typename P = persister_helper_t<T>>
inline static outcome::result<void> store(typename P::ptr_t &item, transaction_t &txn) noexcept {
    auto data = P::serialize(item);
    MDBX_val value;
    value.iov_base = data.data();
    value.iov_len = data.size();
    if constexpr (P::need_synt_key) {
        if (!item->get_db_key()) {
            auto seq = txn.next_sequence();
            if (!seq) {
                return outcome::failure(seq.error());
            }
            item->set_db_key(seq.value());
        }
    }
    auto key = P::get_db_key(item);
    auto r = mdbx_put(txn.txn, txn.dbi, key, &value, MDBX_UPSERT);
    if (r != MDBX_SUCCESS) {
        return make_error_code(r);
    }
    return outcome::success();
}

template <typename T, typename I, typename P = persister_helper_t<T>>
inline static auto load(I &&instantiator, transaction_t &txn) noexcept -> outcome::result<typename P::collection_t> {
    char prefix_mask_buff[1] = {(char)P::prefix};
    std::string_view prefix_mask(prefix_mask_buff, sizeof(P::prefix));
    auto cursor_opt = txn.cursor();
    if (!cursor_opt) {
        return cursor_opt.error();
    }
    auto &cursor = cursor_opt.value();
    typename P::collection_t items;
    auto r = cursor.iterate(prefix_mask, [&](auto &key, auto &value) -> outcome::result<void> {
        using db_type_t = typename P::db_type;
        if constexpr (!std::is_same_v<db_type_t, void>) {
            db_type_t db_item;
            if (!db_item.ParseFromArray(value.data(), value.size())) {
                return outcome::failure(make_error_code(error_code::deserialization_falure));
            }
            auto item = instantiator(key, std::move(db_item));
            P::append(items, std::move(item));
            return outcome::success();
        } else {
            auto item = instantiator(key, value);
            if (item) {
                P::append(items, std::move(item.value()));
                return outcome::success();
            } else {
                return outcome::failure(item.error());
            }
        }
    });
    if (!r) {
        return r.error();
    }

    return std::move(items);
}

template <typename T> struct key_helper_t;

template <> struct key_helper_t<std::uint64_t> {
    static std::uint64_t extract_key(std::string_view key) noexcept {
        std::uint64_t db_key;
        assert(key.size() >= sizeof(db_key) + sizeof(discr_t));
        auto *ptr = key.data() + sizeof(discr_t);
        std::copy(ptr, ptr + sizeof(db_key), reinterpret_cast<char *>(&db_key));
        return db_key;
    }
};

template <> struct key_helper_t<std::string_view> {
    static std::string_view extract_key(std::string_view key) noexcept {
        auto *ptr = key.data() + sizeof(discr_t);
        return std::string_view(ptr, key.size() - sizeof(discr_t));
    }
};

template <typename DB_T, typename T>
auto generic_instantiator = [](std::string_view key, DB_T &&db) {
    using ptr_t = model::intrusive_ptr_t<T>;
    auto db_key = key_helper_t<std::uint64_t>::extract_key(key);
    return ptr_t(new T(std::move(db), db_key));
};

outcome::result<void> store_device(model::device_ptr_t &device, transaction_t &txn) noexcept {
    return store<model::device_t>(device, txn);
}

outcome::result<model::devices_map_t> load_devices(transaction_t &txn) noexcept {
    auto instantiator = generic_instantiator<db::Device, model::device_t>;
    return load<model::device_t>(std::move(instantiator), txn);
}

outcome::result<void> store_folder(model::folder_ptr_t &folder, transaction_t &txn) noexcept {
    return store<model::folder_t>(folder, txn);
}

outcome::result<model::folders_map_t> load_folders(transaction_t &txn) noexcept {
    auto instantiator = generic_instantiator<db::Folder, model::folder_t>;
    return load<model::folder_t>(std::move(instantiator), txn);
}

outcome::result<void> store_folder_info(model::folder_info_ptr_t &info, transaction_t &txn) noexcept {
    return store<model::folder_info_t>(info, txn);
}

outcome::result<model::folder_infos_map_t>
load_folder_infos(model::devices_map_t &devices, model::folders_map_t &folders, transaction_t &txn) noexcept {
    auto instantiator = [&](std::string_view key, db::FolderInfo &&db_item) -> model::folder_info_ptr_t {
        auto device = devices.by_key(db_item.device_key());
        auto folder = folders.by_key(db_item.folder_key());
        assert(folder && device);
        auto db_key = key_helper_t<std::uint64_t>::extract_key(key);
        return new model::folder_info_t(db_item, device.get(), folder.get(), db_key);
    };
    return load<model::folder_info_t>(std::move(instantiator), txn);
}

outcome::result<void> store_file_info(model::file_info_ptr_t &info, transaction_t &txn) noexcept {
    return store<model::file_info_t>(info, txn);
}

outcome::result<model::file_infos_map_t> load_file_infos(model::folder_infos_map_t &folder_infos,
                                                         transaction_t &txn) noexcept {
    auto instantiator = [&](std::string_view key, db::FileInfo &&db_item) -> model::file_info_ptr_t {
        auto db_folder_key = key_helper_t<std::uint64_t>::extract_key(key);
        auto folder_info = folder_infos.by_key(db_folder_key);
        assert(folder_info);
        return new model::file_info_t(db_item, folder_info.get());
    };
    return load<model::file_info_t>(std::move(instantiator), txn);
}

outcome::result<void> store_ignored_device(model::ignored_device_ptr_t &info, transaction_t &txn) noexcept {
    return store<ignored_device_t>(info, txn);
}

outcome::result<model::ignored_devices_map_t> load_ignored_devices(transaction_t &txn) noexcept {
    auto instantiator = [&](std::string_view key, const auto &) -> outcome::result<model::ignored_device_ptr_t> {
        auto id = key_helper_t<std::string_view>::extract_key(key);
        auto device = model::device_id_t::from_sha256(id);
        if (device) {
            return new model::device_id_t(std::move(device.value()));
        } else {
            return outcome::failure(db::make_error_code(db::error_code::invalid_device_id));
        }
    };
    return load<ignored_device_t>(std::move(instantiator), txn);
}

outcome::result<void> store_ignored_folder(model::ignored_folder_ptr_t &info, transaction_t &txn) noexcept {
    return store<model::ignored_folder_t>(info, txn);
}

outcome::result<model::ignored_folders_map_t> load_ignored_folders(transaction_t &txn) noexcept {
    auto instantiator = [&](std::string_view, db::IgnoredFolder &&db_item) -> model::ignored_folder_ptr_t {
        return new model::ignored_folder_t(db_item);
    };
    return load<model::ignored_folder_t>(std::move(instantiator), txn);
}

outcome::result<void> store_block_info(model::block_info_ptr_t &info, transaction_t &txn) noexcept {
    return store<model::block_info_t>(info, txn);
}

outcome::result<model::block_infos_map_t> load_block_infos(transaction_t &txn) noexcept {
    auto instantiator = generic_instantiator<db::BlockInfo, model::block_info_t>;
    return load<model::block_info_t>(std::move(instantiator), txn);
}

outcome::result<void> remove(model::block_infos_map_t &blocks, transaction_t &txn) noexcept {
    using helper = persister_helper_t<model::block_info_t>;
    for (auto &it : blocks) {
        auto &block = it.second;
        auto key = helper::get_db_key(block);
        auto r = mdbx_del(txn.txn, txn.dbi, key, nullptr);
        if (r != MDBX_SUCCESS) {
            return make_error_code(r);
        }
    }
    return outcome::success();
}
#endif

} // namespace syncspirit::db
