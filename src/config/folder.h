#pragma once
#include <cstdint>
#include <string>
#include <set>

namespace syncspirit::config {

enum class folder_type_t { send = 1, receive, send_and_receive, first = send, last = send_and_receive };
enum class pull_order_t { random, alphabetic, smallest, largest, oldest, newest, first = random, last = newest };

struct folder_config_t {
    using device_ids_t = std::set<std::string>;

    std::string id;
    std::string label;
    std::string path;
    device_ids_t device_ids;
    folder_type_t folder_type;
    std::uint32_t rescan_interval;
    pull_order_t pull_order;
    bool watched;
    bool ignore_permissions;
    bool read_only;
    bool ignore_delete;
    bool disable_temp_indixes;
    bool paused;
};

} // namespace syncspirit::config
