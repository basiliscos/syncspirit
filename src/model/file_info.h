#pragma once

#include <cstdint>
#include "arc.hpp"
#include "map.hpp"
#include "structs.pb.h"

namespace syncspirit::model {

struct folder_info_t;

struct file_info_t : arc_base_t<file_info_t> {
    file_info_t(const db::FileInfo &info_, folder_info_t *folder_info_) noexcept;
    ~file_info_t();

    bool operator==(const file_info_t &other) const noexcept { return other.db_key == db_key; }

    db::FileInfo serialize() noexcept;
    bool update(const db::FileInfo &db_info) noexcept;

    inline const std::string &get_db_key() const noexcept { return db_key; }

    inline folder_info_t *get_folder_info() const noexcept { return folder_info; }
    std::string_view get_name() const noexcept;

    inline std::int64_t get_sequence() const noexcept { return sequence; }

  private:
    folder_info_t *folder_info;
    std::string db_key; /* folder_info db key + name */
    std::int64_t sequence;
};

using file_info_ptr_t = intrusive_ptr_t<file_info_t>;

inline const std::string db_key(const file_info_ptr_t &item) noexcept { return item->get_db_key(); }

using file_infos_map_t = generic_map_t<file_info_ptr_t, void, std::string>;

}; // namespace syncspirit::model
