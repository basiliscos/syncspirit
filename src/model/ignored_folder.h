#pragma once

#include "misc/arc.hpp"
#include "misc/map.hpp"

namespace syncspirit::model {

struct ignored_folder_t: arc_base_t<ignored_folder_t> {
    ignored_folder_t(std::string&& folder_id, std::string_view label) noexcept;

    ignored_folder_t(std::string_view key, std::string_view data) noexcept;

    std::string_view get_key() const noexcept;
    std::string_view get_id() const noexcept;
    std::string_view get_label() const noexcept;
    std::string serialize() noexcept;

private:
    std::string label;
    std::string key;
};

using ignored_folder_ptr_t = intrusive_ptr_t<ignored_folder_t>;

using ignored_folders_map_t = generic_map_t<ignored_folder_ptr_t, 1>;

}
