#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"
#include "misc/uuid.h"
#include "device_id.h"
#include "../utils/uri.h"
#include "bep.pb.h"
#include "structs.pb.h"
#include <boost/outcome.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;

struct device_t;
using device_ptr_t = intrusive_ptr_t<device_t>;

struct device_t : arc_base_t<device_t> {
    using uris_t = std::vector<utils::URI>;
    using name_option_t = std::optional<std::string>;

    static outcome::result<device_ptr_t> create(std::string_view key, const db::Device &data) noexcept;
    static outcome::result<device_ptr_t> create(const device_id_t &device_id, std::string_view name,
                                                std::string_view cert_name = "") noexcept;
    virtual ~device_t() = default;

    virtual std::string_view get_key() const noexcept;
    bool operator==(const device_t &other) const noexcept { return other.id == id; }
    bool operator!=(const device_t &other) const noexcept { return other.id != id; }

    std::string serialize() noexcept;
    inline bool is_dynamic() const noexcept { return static_uris.empty(); }
    void mark_online(bool value) noexcept;
    inline bool is_online() const noexcept { return online; }
    inline device_id_t &device_id() noexcept { return id; }
    inline const device_id_t &device_id() const noexcept { return id; }
    inline std::string_view get_name() const noexcept { return name; }
    inline const name_option_t get_cert_name() const noexcept { return cert_name; }
    inline proto::Compression get_compression() const noexcept { return compression; }
    inline bool is_introducer() const noexcept { return introducer; }
    inline bool get_skip_introduction_removals() const noexcept { return skip_introduction_removals; }

    inline const uris_t &get_uris() const noexcept { return uris; }

    void assing_uris(const uris_t &uris) noexcept;

    void update(const db::Device &source) noexcept;
    uint64_t as_uint() noexcept;

  protected:
    device_t(const device_id_t &device_id, std::string_view name, std::string_view cert_name) noexcept;
    template <typename T> void assign(const T &item) noexcept;

    device_id_t id;
    std::string name;
    proto::Compression compression;
    name_option_t cert_name;
    uris_t uris;
    uris_t static_uris;
    bool introducer;
    bool auto_accept;
    bool paused;
    bool skip_introduction_removals;
    bool online = false;
};

struct local_device_t final : device_t {
  public:
    local_device_t(const device_id_t &device_id, std::string_view name, std::string_view cert_name) noexcept;
    std::string_view get_key() const noexcept override;
};

struct devices_map_t : public generic_map_t<device_ptr_t, 2> {
    device_ptr_t by_sha256(std::string_view device_id) const noexcept;
};

} // namespace syncspirit::model

namespace std {

template <> struct hash<syncspirit::model::device_t> {
    inline size_t operator()(const syncspirit::model::device_t &device) const noexcept {
        return std::hash<syncspirit::model::device_id_t>()(device.device_id());
    }
};

template <> struct hash<syncspirit::model::device_ptr_t> {
    inline size_t operator()(const syncspirit::model::device_ptr_t &device) const noexcept {
        return std::hash<syncspirit::model::device_t>()(*device);
    }
};

} // namespace std
