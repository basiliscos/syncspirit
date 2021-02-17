#pragma once
#include <cstdint>
#include <map>
#include "bep.h"
#include "device.h"
#include "dialer.h"
#include "folder.h"
#include "global_announce.h"
#include "local_announce.h"
#include "tui.h"
#include "upnp.h"
#include <boost/filesystem.hpp>

namespace syncspirit::config {

struct main_t {
    using ignored_devices_t = std::set<std::string>;
    using devices_t = std::map<std::string, device_config_t>;
    using folders_t = std::map<std::string, folder_config_t>;

    boost::filesystem::path config_path;
    boost::filesystem::path default_location;
    local_announce_config_t local_announce_config;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
    bep_config_t bep_config;
    dialer_config_t dialer_config;
    tui_config_t tui_config;

    std::uint32_t timeout;
    std::string device_name;
    ignored_devices_t ignored_devices;
    devices_t devices;
    folders_t folders;
};

} // namespace syncspirit::config
