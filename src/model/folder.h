#pragma once

#include <cstdint>
#include <set>
#include <optional>
#include <boost/filesystem.hpp>
#include <boost/outcome.hpp>
#include "../config/main.h"
#include "device.h"
#include "bep.pb.h"
#include "folder_info.h"
#include "misc/storeable.h"
#include "misc/local_file.h"
#include "misc/uuid.h"

namespace syncspirit::model {

namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct cluster_t;

struct folder_t : arc_base_t<folder_t>, storeable_t {
    folder_t(std::string_view key, std::string_view data) noexcept;

    void assign_cluster(cluster_t *cluster) noexcept;
    void add(const folder_info_ptr_t &folder_info) noexcept;
    std::string serialize() noexcept;

    bool operator==(const folder_t &other) const noexcept { return get_id() == other.get_id(); }
    bool operator!=(const folder_t &other) const noexcept { return !(*this == other); }

    std::optional<proto::Folder> get(model::device_ptr_t device) noexcept;
    bool is_shared_with(const model::device_ptr_t &device) noexcept;
    std::int64_t score(const device_ptr_t &peer_device) noexcept;
    folder_info_ptr_t get_folder_info(const device_ptr_t &device) noexcept;

    std::string_view get_key() const noexcept { return std::string_view(key, data_length); }
    std::string_view get_id() const noexcept { return id; }
    const std::string &get_label() noexcept { return label; }
    inline auto &get_folder_infos() noexcept { return folder_infos; }
    inline cluster_t *&get_cluster() noexcept { return cluster; }

    inline const bfs::path &get_path() noexcept { return path; }
    bool update(const proto::Folder &remote) noexcept;
    void update(local_file_map_t &local_files) noexcept;
    proto::Index generate() noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

    static const constexpr size_t data_length = uuid_length + 1;

  private:
    std::string id;
    device_ptr_t device;
    std::string label;
    bfs::path path;
    db::FolderType folder_type;
    std::uint32_t rescan_interval;
    db::PullOrder pull_order;
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

using folder_ptr_t = intrusive_ptr_t<folder_t>;

struct folders_map_t: generic_map_t<folder_ptr_t, 2> {
    folder_ptr_t byId(std::string_view id) noexcept;
};

} // namespace syncspirit::model
