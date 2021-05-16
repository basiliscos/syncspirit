#pragma once
#include <cstdint>
#include <map>
#include "bep.h"
#include "dialer.h"
#include "fs.h"
#include "global_announce.h"
#include "local_announce.h"
#include "tui.h"
#include "upnp.h"
#include <boost/filesystem.hpp>

namespace syncspirit::config {

namespace bfs = boost::filesystem;

struct main_t {
    bfs::path config_path;
    bfs::path default_location;
    local_announce_config_t local_announce_config;
    upnp_config_t upnp_config;
    global_announce_config_t global_announce_config;
    bep_config_t bep_config;
    dialer_config_t dialer_config;
    fs_config_t fs_config;
    tui_config_t tui_config;

    std::uint32_t timeout;
    std::string device_name;
};

} // namespace syncspirit::config
