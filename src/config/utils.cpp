#include "utils.h"

#include <boost/asio/ip/host_name.hpp>
#include <boost/tokenizer.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string.hpp>
#include <spdlog/spdlog.h>
#include "../model/device_id.h"

#define TOML_EXCEPTIONS 0
#include <toml++/toml.h>

namespace fs = boost::filesystem;

static const std::string home_path = "~/.config/syncspirit";

namespace syncspirit::config {

bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept {
    return lhs.rx_buff_size == rhs.rx_buff_size && lhs.connect_timeout == rhs.connect_timeout &&
           lhs.ping_timeout == rhs.ping_timeout;
}

bool operator==(const ignored_folder_config_t &lhs, const ignored_folder_config_t &rhs) noexcept {
    return lhs.id == rhs.id && lhs.label == rhs.label;
}

bool operator==(const device_config_t &lhs, const device_config_t &rhs) noexcept {
    return lhs.id == rhs.id && lhs.name == rhs.name && lhs.compression == rhs.compression &&
           lhs.cert_name == rhs.cert_name && lhs.introducer == rhs.introducer && lhs.auto_accept == rhs.auto_accept &&
           lhs.paused == rhs.paused && lhs.static_addresses == rhs.static_addresses &&
           lhs.skip_introduction_removals == rhs.skip_introduction_removals &&
           lhs.ignored_folders == rhs.ignored_folders;
}

bool operator==(const folder_config_t &lhs, const folder_config_t &rhs) noexcept {
    return lhs.id == rhs.id && lhs.label == rhs.label && lhs.path == rhs.path && lhs.device_ids == rhs.device_ids &&
           lhs.folder_type == rhs.folder_type && lhs.rescan_interval == rhs.rescan_interval &&
           lhs.pull_order == rhs.pull_order && lhs.watched == rhs.watched && lhs.read_only == rhs.read_only &&
           lhs.ignore_permissions == rhs.ignore_permissions && lhs.ignore_delete == rhs.ignore_delete &&
           lhs.disable_temp_indixes == rhs.disable_temp_indixes && lhs.paused == rhs.paused;
}

bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.announce_url == rhs.announce_url && lhs.device_id == rhs.device_id &&
           lhs.cert_file == rhs.cert_file && lhs.key_file == rhs.key_file && lhs.rx_buff_size == rhs.rx_buff_size &&
           lhs.timeout == rhs.timeout && lhs.reannounce_after == rhs.reannounce_after;
}

bool operator==(const local_announce_config_t &lhs, const local_announce_config_t &rhs) noexcept {
    return lhs.enabled == rhs.enabled && lhs.port == rhs.port && lhs.frequency == rhs.frequency;
}

bool operator==(const main_t &lhs, const main_t &rhs) noexcept {
    return lhs.local_announce_config == rhs.local_announce_config && lhs.upnp_config == rhs.upnp_config &&
           lhs.global_announce_config == rhs.global_announce_config && lhs.bep_config == rhs.bep_config &&
           lhs.tui_config == rhs.tui_config && lhs.timeout == rhs.timeout && lhs.device_name == rhs.device_name &&
           lhs.config_path == rhs.config_path && lhs.ignored_devices == rhs.ignored_devices &&
           lhs.devices == rhs.devices && lhs.folders == rhs.folders;
}

bool operator==(const tui_config_t &lhs, const tui_config_t &rhs) noexcept {
    return lhs.refresh_interval == rhs.refresh_interval && lhs.key_quit == rhs.key_quit &&
           lhs.key_more_logs == rhs.key_more_logs && lhs.key_less_logs == rhs.key_less_logs &&
           lhs.key_help == rhs.key_help && lhs.key_config == rhs.key_config;
}

bool operator==(const upnp_config_t &lhs, const upnp_config_t &rhs) noexcept {
    return lhs.discovery_attempts == rhs.discovery_attempts && lhs.max_wait == rhs.max_wait &&
           lhs.timeout == rhs.timeout && lhs.external_port == rhs.external_port && lhs.rx_buff_size == rhs.rx_buff_size;
}

using device_name_t = outcome::result<std::string>;

static std::string expand_home(const std::string &path, const char *home) {
    if (home && path.size() && path[0] == '~') {
        std::string new_path(home);
        new_path += path.c_str() + 1;
        return new_path;
    }
    return path;
}

static device_name_t get_device_name() noexcept {
    boost::system::error_code ec;
    auto device_name = boost::asio::ip::host_name(ec);
    if (ec) {
        return ec;
    }
    return device_name;
}

static std::optional<device_config_t> get_device(toml::table &t) noexcept {
    using result_t = std::optional<device_config_t>;
    auto id = t["id"].value<std::string>();
    if (!id) {
        return result_t();
    }

    auto device_id = model::device_id_t::from_string(id.value());
    if (!device_id) {
        return result_t();
    }

    auto name = t["name"].value<std::string>();
    if (!name) {
        return result_t();
    }

    auto compression = t["compression"].value<std::uint32_t>();
    if (!compression) {
        return result_t();
    }
    auto compr_value = static_cast<compression_t>(compression.value());
    if (compr_value < compression_t::min || compr_value > compression_t::max) {
        return result_t();
    }

    auto introducer = t["introducer"].value<bool>();
    if (!introducer) {
        return result_t();
    }

    auto auto_accept = t["auto_accept"].value<bool>();
    if (!auto_accept) {
        return result_t();
    }

    auto paused = t["paused"].value<bool>();
    if (!paused) {
        return result_t();
    }

    auto skip_introduction_removals = t["skip_introduction_removals"].value<bool>();
    if (!paused) {
        return result_t();
    }

    device_config_t::cert_name_t cert_name;
    auto cn = t["cert_name"].value<std::string>();
    if (cn) {
        cert_name = cn.value();
    }

    auto addresses = t["addresses"];
    device_config_t::addresses_t device_addresses;
    if (addresses.is_array()) {
        auto arr = addresses.as_array();
        for (size_t i = 0; i < arr->size(); ++i) {
            auto node = arr->get(i);
            if (node->is_string()) {
                auto &value = node->as_string()->get();
                auto url = utils::parse(value.c_str());
                if (url) {
                    device_addresses.emplace_back(std::move(url.value()));
                } else {
                    spdlog::warn("invalid url : {}, ignored", value);
                }
            }
        }
    }

    auto folders = t["ignored_folder"];
    device_config_t::ignored_folders_t ignored_folders;
    if (folders.is_array_of_tables()) {
        auto arr = folders.as_array();
        for (size_t i = 0; i < arr->size(); ++i) {
            auto node = arr->get(i);
            if (node->is_table()) {
                auto folder = *node->as_table();
                auto id = folder["id"].value<std::string>();
                auto label = folder["label"].value<std::string>();
                if (id && label) {
                    ignored_folders.emplace(ignored_folder_config_t{id.value(), label.value()});
                }
            }
        }
    }

    return device_config_t{
        id.value(),
        name.value(),
        compr_value,
        cert_name,
        introducer.value(),
        auto_accept.value(),
        paused.value(),
        skip_introduction_removals.value(),
        std::move(device_addresses),
        std::move(ignored_folders),
    };
}

static std::optional<folder_config_t> get_folder(toml::table &t, const main_t::devices_t &devices) noexcept {
    using result_t = std::optional<folder_config_t>;
    auto id = t["id"].value<std::string>();
    if (!id) {
        return result_t();
    }

    auto label = t["label"].value<std::string>();
    if (!label) {
        return result_t();
    }

    auto path = t["path"].value<std::string>();
    if (!path) {
        return result_t();
    }

    auto ft = t["folder_type"].value<std::uint32_t>();
    if (!ft) {
        return result_t();
    }
    bool ft_not_fits = ft.value() < static_cast<std::uint32_t>(folder_type_t::first) ||
                       ft.value() > static_cast<std::uint32_t>(folder_type_t::last);
    if (ft_not_fits) {
        return result_t();
    }
    folder_type_t folder_type(static_cast<folder_type_t>(ft.value()));

    auto rescan_interval = t["rescan_interval"].value<std::uint32_t>();
    if (!rescan_interval) {
        return result_t();
    }

    auto po = t["pull_order"].value<std::uint32_t>();
    if (!po) {
        return result_t();
    }
    bool po_not_fits = po.value() < static_cast<std::uint32_t>(pull_order_t::first) ||
                       po.value() > static_cast<std::uint32_t>(pull_order_t::last);
    if (po_not_fits) {
        return result_t();
    }
    pull_order_t pull_order(static_cast<pull_order_t>(po.value()));

    auto watched = t["watched"].value<bool>();
    if (!watched) {
        return result_t();
    }

    auto ignore_permissions = t["ignore_permissions"].value<bool>();
    if (!ignore_permissions) {
        return result_t();
    }

    auto read_only = t["read_only"].value<bool>();
    if (!read_only) {
        return result_t();
    }

    auto ignore_delete = t["ignore_delete"].value<bool>();
    if (!ignore_delete) {
        return result_t();
    }

    auto disable_temp_indixes = t["disable_temp_indixes"].value<bool>();
    if (!disable_temp_indixes) {
        return result_t();
    }

    auto paused = t["paused"].value<bool>();
    if (!paused) {
        return result_t();
    }

    folder_config_t::device_ids_t device_ids;
    auto devs = t["devices"];
    if (devs.is_array()) {
        auto arr = devs.as_array();
        for (size_t i = 0; i < arr->size(); ++i) {
            auto node = arr->get(i);
            if (node->is_string()) {
                auto &value = node->as_string()->get();
                if (devices.count(value)) {
                    device_ids.emplace(value);
                } else {
                    spdlog::warn("unknown device: {}, for folder {} / {}", value, label.value(), id.value());
                }
            }
        }
    }

    return folder_config_t{id.value(),
                           label.value(),
                           path.value(),
                           std::move(device_ids),
                           folder_type,
                           rescan_interval.value(),
                           pull_order,
                           watched.value(),
                           ignore_permissions.value(),
                           read_only.value(),
                           ignore_delete.value(),
                           disable_temp_indixes.value(),
                           paused.value()};
}

config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path) {
    main_t cfg;
    cfg.config_path = config_path;

    auto home = std::getenv("HOME");
    auto r = toml::parse(config);
    if (!r) {
        return std::string(r.error().description());
    }

    auto &root_tbl = r.table();
    // main
    {
        auto t = root_tbl["main"];
        auto &c = cfg;
        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "main/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();

        auto device_name = t["device_name"].value<std::string>();
        if (!device_name) {
            auto option = get_device_name();
            if (!option)
                return option.error().message();
            device_name = option.value();
        }
        c.device_name = device_name.value();

        auto default_location = t["default_location"].value<std::string>();
        if (!default_location) {
            return "main/default_location is incorrect or missing";
        }
        c.default_location = default_location.value();

        auto ignored_devices = t["ignored_devices"];
        if (ignored_devices.is_array()) {
            auto arr = ignored_devices.as_array();
            for (size_t i = 0; i < arr->size(); ++i) {
                auto node = arr->get(i);
                if (node->is_string()) {
                    auto &value = node->as_string()->get();
                    auto device_id = model::device_id_t::from_string(value);
                    if (device_id) {
                        c.ignored_devices.insert(value);
                    } else {
                        spdlog::warn("invalid device_id : {}, ignored", value);
                    }
                }
            }
        }
    };

    // local_discovery
    {
        auto t = root_tbl["local_discovery"];
        auto &c = cfg.local_announce_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "local_discovery/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto port = t["port"].value<std::uint16_t>();
        if (!port) {
            return "local_discovery/port is incorrect or missing";
        }
        c.port = port.value();

        auto frequency = t["frequency"].value<std::uint32_t>();
        if (!frequency) {
            return "local_discovery/frequency is incorrect or missing";
        }
        c.frequency = frequency.value();
    }

    // global_discovery
    {
        auto t = root_tbl["global_discovery"];
        auto &c = cfg.global_announce_config;

        auto enabled = t["enabled"].value<bool>();
        if (!enabled) {
            return "global_discovery/enabled is incorrect or missing";
        }
        c.enabled = enabled.value();

        auto url = t["announce_url"].value<std::string>();
        if (!url) {
            return "global_discovery/announce_url is incorrect or missing";
        }
        auto announce_url = utils::parse(url.value().c_str());
        if (!announce_url) {
            return "global_discovery/announce_url is not url";
        }
        c.announce_url = announce_url.value();

        auto device_id = t["device_id"].value<std::string>();
        if (!device_id) {
            return "global_discovery/device_id is incorrect or missing";
        }
        c.device_id = device_id.value();

        auto cert_file = t["cert_file"].value<std::string>();
        if (!cert_file) {
            return "global_discovery/cert_file is incorrect or missing";
        }
        c.cert_file = expand_home(cert_file.value(), home);

        auto key_file = t["key_file"].value<std::string>();
        if (!key_file) {
            return "global_discovery/key_file is incorrect or missing";
        }
        c.key_file = expand_home(key_file.value(), home);

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "global_discovery/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();

        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "global_discovery/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();
    };

    // upnp
    {
        auto t = root_tbl["upnp"];
        auto &c = cfg.upnp_config;
        auto max_wait = t["max_wait"].value<std::uint32_t>();
        if (!max_wait) {
            return "global_discovery/max_wait is incorrect or missing";
        }
        c.max_wait = max_wait.value();

        auto discovery_attempts = t["discovery_attempts"].value<std::uint32_t>();
        if (!discovery_attempts) {
            return "global_discovery/discovery_attempts is incorrect or missing";
        }
        c.discovery_attempts = discovery_attempts.value();

        auto timeout = t["timeout"].value<std::uint32_t>();
        if (!timeout) {
            return "upnp/timeout is incorrect or missing";
        }
        c.timeout = timeout.value();

        auto external_port = t["external_port"].value<std::uint32_t>();
        if (!external_port) {
            return "upnp/external_port is incorrect or missing";
        }
        c.external_port = external_port.value();

        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "upng/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();
    };

    // bep
    {
        auto t = root_tbl["bep"];
        auto &c = cfg.bep_config;
        auto rx_buff_size = t["rx_buff_size"].value<std::uint32_t>();
        if (!rx_buff_size) {
            return "bep/rx_buff_size is incorrect or missing";
        }
        c.rx_buff_size = rx_buff_size.value();

        auto connect_timeout = t["connect_timeout"].value<std::uint32_t>();
        if (!connect_timeout) {
            return "bep/connect_timeout is incorrect or missing";
        }
        c.connect_timeout = connect_timeout.value();

        auto ping_timeout = t["ping_timeout"].value<std::uint32_t>();
        if (!ping_timeout) {
            return "bep/oing_timeout is incorrect or missing";
        }
        c.ping_timeout = ping_timeout.value();
    }

    // tui
    {
        auto t = root_tbl["tui"];
        auto &c = cfg.tui_config;
        auto refresh_interval = t["refresh_interval"].value<std::uint32_t>();
        if (!refresh_interval) {
            return "tui/refresh_interval is incorrect or missing";
        }
        c.refresh_interval = refresh_interval.value();

        auto key_quit = t["key_quit"].value<std::string>();
        if (!key_quit || key_quit.value().empty()) {
            return "tui/key_quit is incorrect or missing";
        }
        c.key_quit = key_quit.value()[0];

        auto key_more_logs = t["key_more_logs"].value<std::string>();
        if (!key_more_logs || key_more_logs.value().empty()) {
            return "tui/key_more_logs is incorrect or missing";
        }
        c.key_more_logs = key_more_logs.value()[0];

        auto key_less_logs = t["key_less_logs"].value<std::string>();
        if (!key_less_logs || key_less_logs.value().empty()) {
            return "tui/key_less_logs is incorrect or missing";
        }
        c.key_less_logs = key_less_logs.value()[0];

        auto key_config = t["key_config"].value<std::string>();
        if (!key_config || key_config.value().empty()) {
            return "tui/key_config is incorrect or missing";
        }
        c.key_config = key_config.value()[0];

        auto key_help = t["key_help"].value<std::string>();
        if (!key_help || key_help.value().empty()) {
            return "tui/key_help is incorrect or missing";
        }
        c.key_help = key_help.value()[0];
    }

    // devices
    {
        auto td = root_tbl["device"];
        if (td.is_array_of_tables()) {
            auto arr = td.as_array();
            for (size_t i = 0; i < arr->size(); ++i) {
                auto node = arr->get(i);
                auto &value = *node->as_table();
                auto device = get_device(value);
                if (device) {
                    auto id = device->id;
                    cfg.devices.emplace(id, std::move(device.value()));
                }
            }
        }
    }

    // folders
    {
        auto &devices = cfg.devices;
        auto td = root_tbl["folder"];
        if (td.is_array_of_tables()) {
            auto arr = td.as_array();
            for (size_t i = 0; i < arr->size(); ++i) {
                auto node = arr->get(i);
                auto &value = *node->as_table();
                auto folder = get_folder(value, devices);
                if (folder) {
                    auto id = folder.value().id;
                    cfg.folders.emplace(id, std::move(folder.value()));
                }
            }
        }
    }

    return std::move(cfg);
}

outcome::result<void> serialize(const main_t cfg, std::ostream &out) noexcept {
    auto ignored_devices = toml::array{};
    for (auto &device_id : cfg.ignored_devices) {
        ignored_devices.emplace_back<std::string>(device_id);
    }
    auto devices = toml::array{};
    auto folders = toml::array{};
    // clang-format off
    for (auto &it : cfg.devices) {
        auto &device = it.second;
        auto device_table = toml::table{{
            {"id", device.id},
            {"name", device.name},
            {"compression", static_cast<std::uint32_t>(device.compression)},
            {"introducer", device.introducer},
            {"auto_accept", device.auto_accept},
            {"paused", device.paused},
            {"skip_introduction_removals", device.skip_introduction_removals},
        }};
        if (device.cert_name) {
            device_table.insert("cert_name", device.cert_name.value());
        }
        if (!device.static_addresses.empty()) {
            auto addresses = toml::array{};
            for(auto& url: device.static_addresses) {
                addresses.emplace_back<std::string>(url.full);
            }
            device_table.insert("addresses", addresses);
        }
        if (!device.ignored_folders.empty()) {
            auto ignored_folders = toml::array{};
            for(auto& folder: device.ignored_folders) {
                ignored_folders.push_back(toml::table{{
                    {"id", folder.id},
                    {"label", folder.label}
                }});
            }
            device_table.insert("ignored_folder", ignored_folders);
        }
        devices.push_back(device_table);
    }
    for (auto &it : cfg.folders) {
        auto& folder = it.second;
        auto folder_table = toml::table{{
            {"id", folder.id},
            {"label", folder.label},
            {"path", folder.path},
            {"folder_type", static_cast<std::uint32_t>(folder.folder_type)},
            {"rescan_interval", folder.rescan_interval},
            {"pull_order", static_cast<std::uint32_t>(folder.pull_order)},
            {"watched", folder.watched},
            {"ignore_permissions", folder.ignore_permissions},
            {"read_only", folder.read_only},
            {"ignore_delete", folder.ignore_delete},
            {"disable_temp_indixes", folder.disable_temp_indixes},
            {"paused", folder.paused},
        }};
        auto device_ids = toml::array{};
        for(auto& it: folder.device_ids) {
            device_ids.emplace_back<std::string>(it);
        }
        folder_table.insert("devices", device_ids);
        folders.push_back(folder_table);
    }
    auto tbl = toml::table{{
        {"main", toml::table{{
            {"timeout",  cfg.timeout},
            {"device_name", cfg.device_name},
            {"ignored_devices", ignored_devices},
            {"default_location", cfg.default_location.c_str()},
        }}},
        {"local_discovery", toml::table{{
            {"enabled",  cfg.local_announce_config.enabled},
            {"port",  cfg.local_announce_config.port},
            {"frequency",  cfg.local_announce_config.frequency},
        }}},
        {"global_discovery", toml::table{{
            {"enabled",  cfg.global_announce_config.enabled},
            {"announce_url", cfg.global_announce_config.announce_url.full},
            {"device_id", cfg.global_announce_config.device_id},
            {"cert_file", cfg.global_announce_config.cert_file},
            {"key_file", cfg.global_announce_config.key_file},
            {"rx_buff_size", cfg.global_announce_config.rx_buff_size},
            {"timeout",  cfg.global_announce_config.timeout},
        }}},
        {"upnp", toml::table{{
            {"discovery_attempts", cfg.upnp_config.discovery_attempts},
            {"max_wait", cfg.upnp_config.max_wait},
            {"timeout",  cfg.upnp_config.timeout},
            {"external_port",  cfg.upnp_config.external_port},
            {"rx_buff_size", cfg.upnp_config.rx_buff_size},
        }}},
        {"bep", toml::table{{
            {"rx_buff_size", cfg.bep_config.rx_buff_size},
            {"connect_timeout", cfg.bep_config.connect_timeout},
            {"ping_timeout", cfg.bep_config.ping_timeout},
        }}},
        {"tui", toml::table{{
            {"refresh_interval", cfg.tui_config.refresh_interval},
            {"key_quit", std::string_view(&cfg.tui_config.key_quit, 1)},
            {"key_more_logs", std::string_view(&cfg.tui_config.key_more_logs, 1)},
            {"key_less_logs", std::string_view(&cfg.tui_config.key_less_logs, 1)},
            {"key_config", std::string_view(&cfg.tui_config.key_config, 1)},
            {"key_help", std::string_view(&cfg.tui_config.key_help, 1)},
        }}},
    }};
    if (!devices.empty()) {
        tbl.insert("device", devices);
    }
    if (!folders.empty()) {
        tbl.insert("folder", folders);
    }
    // clang-format on
    out << tbl;
    return outcome::success();
}

main_t generate_config(const boost::filesystem::path &config_path) {
    auto dir = config_path.parent_path();
    if (!fs::exists(dir)) {
        spdlog::info("creating directory {}", dir.c_str());
        fs::create_directories(dir);
    }

    std::string cert_file = home_path + "/cert.pem";
    std::string key_file = home_path + "/key.pem";
    auto home = std::getenv("HOME");
    auto config_dir = fs::path(home).append(".config").append("syncthing");
    bool is_home = dir == fs::path(config_dir);
    if (!is_home) {
        using boost::algorithm::replace_all_copy;
        cert_file = replace_all_copy(cert_file, home_path, dir.string());
        key_file = replace_all_copy(key_file, home_path, dir.string());
    }

    auto device_name = get_device_name();
    auto device = std::string(device_name ? device_name.value() : "localhost");

    // clang-format off
    main_t cfg;
    cfg.config_path = config_path;
    cfg.default_location = fs::path(home);
    cfg.timeout = 5000;
    cfg.device_name = device;
    cfg.local_announce_config = local_announce_config_t {
        true,
        21027,
        30
    };
    cfg.global_announce_config = global_announce_config_t{
        true,
        utils::parse("https://discovery.syncthing.net/").value(),
        "LYXKCHX-VI3NYZR-ALCJBHF-WMZYSPK-QG6QJA3-MPFYMSO-U56GTUK-NA2MIAW",
        cert_file,
        key_file,
        32 * 1024,
        3000,
        10 * 60,
    };
    cfg.upnp_config = upnp_config_t {
        2,          /* discovery_attempts */
        1,          /* max_wait */
        10,         /* timeout */
        22001,      /* external port */
        64 * 1024,  /* rx_buff */
    };
    cfg.bep_config = bep_config_t {
        16 * 1024 * 1024,   /* rx_buff */
        5000,               /* connect_timeout */
        90000,              /* ping_timeout */
    };
    cfg.tui_config = tui_config_t {
        100,   /* refresh_interval */
        'q',   /* key_quit */
        '+',   /* key_more_logs */
        '-',   /* key_less_logs */
        '?',   /* key_help */
        'c'    /* key_config */
    };
    return cfg;
}


}
