#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <set>
#include "../utils/uri.h"

namespace syncspirit::config {

enum class compression_t { none = 1, meta, all, min = none, max = all };

struct ignored_folder_config_t {
    std::string id;
    std::string label;
};

} // namespace syncspirit::config

namespace std {
template <> struct less<syncspirit::config::ignored_folder_config_t> {
    using folder_t = syncspirit::config::ignored_folder_config_t;
    inline bool operator()(const folder_t &lhs, const folder_t &rhs) const noexcept { return lhs.id < rhs.id; }
};
} // namespace std

namespace syncspirit::config {

struct device_config_t {
    using addresses_t = std::vector<utils::URI>;
    using cert_name_t = std::optional<std::string>;
    using ignored_folders_t = std::set<ignored_folder_config_t>;

    std::string id;
    std::string name;
    compression_t compression;
    cert_name_t cert_name;
    bool introducer;
    bool auto_accept;
    bool paused;
    bool skip_introduction_removals;
    addresses_t static_addresses;
    ignored_folders_t ignored_folders;
};

} // namespace syncspirit::config
