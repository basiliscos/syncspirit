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
    folder_t(const db::Folder &db_folder, const uuid_t& uuid) noexcept;
    void assign_device(model::device_ptr_t device_) noexcept;
    void assign_cluster(cluster_t *cluster) noexcept;
    void add(const folder_info_ptr_t &folder_info) noexcept;
    db::Folder serialize() noexcept;

    bool operator==(const folder_t &other) const noexcept { return other._id == _id; }
    bool operator!=(const folder_t &other) const noexcept { return other._id != _id; }

    std::optional<proto::Folder> get(model::device_ptr_t device) noexcept;
    bool is_shared_with(const model::device_ptr_t &device) noexcept;
    std::int64_t score(const device_ptr_t &peer_device) noexcept;
    folder_info_ptr_t get_folder_info(const device_ptr_t &device) noexcept;

    const std::string &id() noexcept { return _id; }
    const std::string &label() noexcept { return _label; }
    inline auto &get_folder_infos() noexcept { return folder_infos; }
    inline cluster_t *&get_cluster() noexcept { return cluster; }
    inline const bfs::path &get_path() noexcept { return path; }
    bool update(const proto::Folder &remote) noexcept;
    void update(local_file_map_t &local_files) noexcept;
    proto::Index generate() noexcept;

    template <typename T> auto &access() noexcept;
    template <typename T> auto &access() const noexcept;

  private:
    device_ptr_t device;
    std::string _id;
    std::string _label;
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
    char uuid[uuid_length];
};

using folder_ptr_t = intrusive_ptr_t<folder_t>;

using folders_map_t = generic_map_t<folder_ptr_t, 1>;

struct ignored_folder_t : arc_base_t<ignored_folder_t> {
    ignored_folder_t(const db::IgnoredFolder &folder) noexcept;

    bool operator==(const ignored_folder_t &other) const noexcept { return other.id == id; }
    bool operator!=(const ignored_folder_t &other) const noexcept { return other.id != id; }

    db::IgnoredFolder serialize() const noexcept;

    std::string id;
    std::string label;
};

using ignored_folder_ptr_t = intrusive_ptr_t<ignored_folder_t>;
using ignored_folders_map_t = generic_map_t<ignored_folder_ptr_t, 1>;

} // namespace syncspirit::model
