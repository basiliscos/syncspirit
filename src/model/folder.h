#pragma once

#include <cstdint>
#include <optional>
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include "device.h"
#include "bep.pb.h"
#include "structs.pb.h"
#include "folder_info.h"
#include "misc/local_file.h"
#include "misc/uuid.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_t;
using cluster_ptr_t = intrusive_ptr_t<cluster_t>;

struct folder_t;

using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct folder_t final : arc_base_t<folder_t> {

    enum class foldet_type_t { send = 0, receive, send_and_receive };
    enum class pull_order_t { random = 0, alphabetic, largest, oldest, newest };

    static outcome::result<folder_ptr_t> create(std::string_view key, const db::Folder &folder) noexcept;
    static outcome::result<folder_ptr_t> create(const uuid_t &uuid, const db::Folder &folder) noexcept;

    void assign_cluster(const cluster_ptr_t &cluster) noexcept;
    void add(const folder_info_ptr_t &folder_info) noexcept;
    std::string serialize() noexcept;

    bool operator==(const folder_t &other) const noexcept { return get_id() == other.get_id(); }
    bool operator!=(const folder_t &other) const noexcept { return !(*this == other); }

    bool is_shared_with(const device_t &device) const noexcept;
    std::int64_t score(const device_ptr_t &peer_device) noexcept;

    std::string_view get_key() const noexcept { return std::string_view(key, data_length); }
    std::string_view get_id() const noexcept { return id; }
    const std::string &get_label() noexcept { return label; }
    inline auto &get_folder_infos() noexcept { return folder_infos; }
    inline cluster_t *&get_cluster() noexcept { return cluster; }

    inline const bfs::path &get_path() noexcept { return path; }
    void update(local_file_map_t &local_files) noexcept;
    std::optional<proto::Folder> generate(const model::device_t &device) const noexcept;
    proto::Index generate() noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

    static const constexpr size_t data_length = uuid_length + 1;

  private:
    folder_t(std::string_view key) noexcept;
    folder_t(const uuid_t &uuid) noexcept;
    void assign_fields(const db::Folder &item) noexcept;

    std::string id;
    device_ptr_t device;
    std::string label;
    bfs::path path;
    foldet_type_t folder_type;
    std::uint32_t rescan_interval;
    pull_order_t pull_order;
    bool watched;
    bool read_only;
    bool ignore_permissions;
    bool ignore_delete;
    bool disable_temp_indixes;
    bool paused;
    folder_infos_map_t folder_infos;
    cluster_t *cluster = nullptr;
    char key[data_length];
};

struct folders_map_t : generic_map_t<folder_ptr_t, 2> {
    folder_ptr_t by_id(std::string_view id) const noexcept;
};

} // namespace syncspirit::model
