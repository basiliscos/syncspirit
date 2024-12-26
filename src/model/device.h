// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "misc/augmentation.hpp"
#include "misc/map.hpp"
#include "misc/uuid.h"
#include "device_id.h"
#include "remote_folder_info.h"
#include "utils/uri.h"
#include "syncspirit-export.h"
#include "bep.pb.h"
#include "structs.pb.h"
#include <boost/outcome.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace syncspirit::model {

namespace outcome = boost::outcome_v2;
namespace pt = boost::posix_time;

struct device_t;
struct file_iterator_t;
struct cluster_t;
using device_ptr_t = intrusive_ptr_t<device_t>;
using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

enum class device_state_t { offline, discovering, connecting, online };

struct SYNCSPIRIT_API device_t : augmentable_t<device_t> {
    using uris_t = utils::uri_container_t;
    using name_option_t = std::optional<std::string>;
    using tcp = boost::asio::ip::tcp;

    static outcome::result<device_ptr_t> create(std::string_view key, const db::Device &data) noexcept;
    static outcome::result<device_ptr_t> create(const device_id_t &device_id, std::string_view name,
                                                std::string_view cert_name = "") noexcept;
    virtual ~device_t();

    virtual std::string_view get_key() const noexcept;
    bool operator==(const device_t &other) const noexcept { return other.id == id; }
    bool operator!=(const device_t &other) const noexcept { return other.id != id; }

    std::string serialize(db::Device &device) const noexcept;
    std::string serialize() const noexcept;
    inline bool is_dynamic() const noexcept { return static_uris.empty(); }
    inline device_state_t get_state() const noexcept { return state; }
    void update_state(device_state_t new_state) noexcept;
    void update_contact(const tcp::endpoint &endpoint, std::string_view client_name,
                        std::string_view client_version) noexcept;
    inline device_id_t &device_id() noexcept { return id; }
    inline const device_id_t &device_id() const noexcept { return id; }
    inline std::string_view get_name() const noexcept { return name; }
    inline const name_option_t get_cert_name() const noexcept { return cert_name; }
    inline const tcp::endpoint &get_endpoint() const noexcept { return endpoint; }
    inline std::string_view get_client_name() const noexcept { return client_name; }
    inline std::string_view get_client_version() const noexcept { return client_version; }
    inline proto::Compression get_compression() const noexcept { return compression; }
    inline bool is_introducer() const noexcept { return introducer; }
    inline bool has_auto_accept() const noexcept { return auto_accept; }
    inline bool is_paused() const noexcept { return paused; }
    inline bool get_skip_introduction_removals() const noexcept { return skip_introduction_removals; }
    inline auto &get_remote_folder_infos() noexcept { return remote_folder_infos; }
    inline const pt::ptime &get_last_seen() const noexcept { return last_seen; }

    inline const uris_t &get_uris() const noexcept { return uris; }
    inline const uris_t &get_static_uris() const noexcept { return static_uris; }
    void set_static_uris(uris_t) noexcept;

    void assign_uris(const uris_t &uris) noexcept;

    void update(const db::Device &source) noexcept;

    file_iterator_ptr_t create_iterator(cluster_t &) noexcept;
    void release_iterator(file_iterator_ptr_t &) noexcept;
    file_iterator_t *get_iterator() noexcept;

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
    device_state_t state = device_state_t::offline;
    tcp::endpoint endpoint;
    std::string client_name;
    std::string client_version;

    file_iterator_ptr_t iterator;

    remote_folder_infos_map_t remote_folder_infos;
    pt::ptime last_seen;
};

struct SYNCSPIRIT_API local_device_t final : device_t {
  public:
    local_device_t(const device_id_t &device_id, std::string_view name, std::string_view cert_name) noexcept;
    std::string_view get_key() const noexcept override;
};

struct SYNCSPIRIT_API devices_map_t : public generic_map_t<device_ptr_t, 2> {
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
