#pragma once

#include <unordered_map>
#include "arc.hpp"
#include "map.hpp"
#include "device_id.h"
#include "../utils/uri.h"
#include "structs.pb.h"
#include "bep.pb.h"

namespace syncspirit::model {

struct device_t : arc_base_t<device_t> {
    using static_addresses_t = std::vector<utils::URI>;

    device_t(const db::Device &db_device, std::uint64_t db_key_ = 0) noexcept;

    device_id_t device_id;
    std::string name;
    proto::Compression compression;
    std::optional<std::string> cert_name;
    static_addresses_t static_addresses;
    bool introducer;
    bool auto_accept;
    bool paused;
    bool skip_introduction_removals;

    bool operator==(const device_t &other) const noexcept { return other.device_id == device_id; }
    bool operator!=(const device_t &other) const noexcept { return other.device_id != device_id; }

    db::Device serialize() noexcept;
    inline bool is_dynamic() const noexcept { return static_addresses.empty(); }
    void mark_online(bool value) noexcept;
    inline bool is_online() const noexcept { return online; }

    virtual const std::string &get_id() const noexcept;
    inline std::uint64_t get_db_key() const noexcept { return db_key; }
    inline void set_db_key(std::uint64_t value) noexcept { db_key = value; }

  private:
    bool online = false;
    std::uint64_t db_key;
};

using device_ptr_t = intrusive_ptr_t<device_t>;
using ignored_device_ptr_t = intrusive_ptr_t<device_id_t>;

inline const std::string &natural_key(const device_ptr_t &device) noexcept { return device->get_id(); }
inline std::uint64_t db_key(const device_ptr_t &device) noexcept { return device->get_db_key(); }
inline const std::string &db_key(const ignored_device_ptr_t &device) noexcept { return device->get_sha256(); }

struct local_device_t : device_t {
    using device_t::device_t;
    const std::string &get_id() const noexcept override;
};

using devices_map_t = generic_map_t<device_ptr_t, std::string>;
using ignored_devices_map_t = generic_map_t<ignored_device_ptr_t, void, std::string>;

} // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::device_t> {
    inline size_t operator()(const syncspirit::model::device_t &device) const noexcept {
        return std::hash<syncspirit::model::device_id_t>()(device.device_id);
    }
};

template <> struct hash<syncspirit::model::device_ptr_t> {
    inline size_t operator()(const syncspirit::model::device_ptr_t &device) const noexcept {
        return std::hash<syncspirit::model::device_t>()(*device);
    }
};

} // namespace std
