#include "category.h"

#include "properties.h"

#include <charconv>
#include <cassert>

namespace syncspirit::fltk::config {

category_t::category_t(std::string label_, std::string explanation_, properties_t properties_)
    : label{std::move(label_)}, explanation{std::move(explanation_)}, properties{std::move(properties_)} {}

const std::string &category_t::get_label() const { return label; }
const std::string &category_t::get_explanation() const { return explanation; }
const properties_t &category_t::get_properties() const { return properties; }

auto reflect(const main_cfg_t &config, const main_cfg_t &default_config) -> categories_t {
    auto r = categories_t{};

    auto c_bep = [&]() -> category_ptr_t {
        auto &bep = config.bep_config;
        auto &bep_def = default_config.bep_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new bep::blocks_max_requested_t(bep.blocks_max_requested, bep_def.blocks_max_requested)),
            property_ptr_t(new bep::blocks_simultaneous_write_t(bep.blocks_simultaneous_write, bep_def.blocks_simultaneous_write)),
            property_ptr_t(new bep::connect_timeout_t(bep.connect_timeout, bep_def.connect_timeout)),
            property_ptr_t(new bep::request_timeout_t(bep.request_timeout, bep_def.request_timeout)),
            property_ptr_t(new bep::rx_buff_size_t(bep.rx_buff_size, bep_def.rx_buff_size)),
            property_ptr_t(new bep::rx_timeout_t(bep.rx_timeout, bep_def.rx_timeout)),
            property_ptr_t(new bep::tx_buff_limit_t(bep.tx_buff_limit, bep_def.tx_buff_limit)),
            property_ptr_t(new bep::tx_timeout_t(bep.tx_timeout, bep_def.tx_timeout)),
            // clang-format on
        };
        return new category_t("bep", "BEP protocol/network settings", std::move(props));
    }();

    auto c_db = [&]() -> category_ptr_t {
        auto &db = config.db_config;
        auto &db_def = default_config.db_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new db::uncommited_threshold_t(db.uncommitted_threshold, db_def.uncommitted_threshold)),
            property_ptr_t(new db::upper_limit_t(db.upper_limit, db_def.upper_limit)),
            // clang-format on
        };
        return new category_t("db", "database settings", std::move(props));
    }();

    auto c_dialer = [&]() -> category_ptr_t {
        auto &d = config.dialer_config;
        auto &d_def = default_config.dialer_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new dialer::enabled_t(d.enabled, d_def.enabled)),
            property_ptr_t(new dialer::redial_timeout_t(d.redial_timeout, d_def.redial_timeout)),
            // clang-format on
        };
        return new category_t("dialer", "outbound connection scheduler settings", std::move(props));
    }();

    auto c_fs = [&]() -> category_ptr_t {
        auto &f = config.fs_config;
        auto &f_def = default_config.fs_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new fs::mru_size_t(f.mru_size, f_def.mru_size)),
            property_ptr_t(new fs::temporally_timeout_t(f.temporally_timeout, f_def.temporally_timeout)),
            // clang-format on
        };
        return new category_t("fs", "filesystem settings", std::move(props));
    }();

    auto c_gd = [&]() -> category_ptr_t {
        auto &g = config.global_announce_config;
        auto &g_def = default_config.global_announce_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new global_discovery::enabled_t(g.enabled, g_def.enabled)),
            property_ptr_t(new global_discovery::cert_file_t(g.cert_file, g_def.cert_file)),
            property_ptr_t(new global_discovery::key_file_t(g.key_file, g_def.key_file)),
            property_ptr_t(new global_discovery::announce_url_t(g.announce_url.full, g_def.announce_url.full)),
            property_ptr_t(new global_discovery::device_id_t(g.device_id, g_def.device_id)),
            property_ptr_t(new global_discovery::rx_buff_size_t(g.rx_buff_size, g_def.rx_buff_size)),
            property_ptr_t(new global_discovery::timeout_t(g.timeout, g_def.timeout)),
            // clang-format on
        };
        return new category_t("global_discovery", "global peer discovery settings", std::move(props));
    }();

    auto c_ld = [&]() -> category_ptr_t {
        auto &l = config.local_announce_config;
        auto &l_def = default_config.local_announce_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new local_discovery::enabled_t(l.enabled, l_def.enabled)),
            property_ptr_t(new local_discovery::frequency_t(l.frequency, l_def.frequency)),
            property_ptr_t(new local_discovery::port_t(l.port, l_def.port)),
            // clang-format on
        };
        return new category_t("local_discovery", "LAN peer discovery settings", std::move(props));
    }();

    auto c_main = [&]() -> category_ptr_t {
        auto &l = config;
        auto &l_def = default_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new main::default_location_t(l.default_location.string(), l_def.default_location.string())),
            property_ptr_t(new main::device_name_t(l.device_name, l_def.device_name)),
            property_ptr_t(new main::hasher_threads_t(l.hasher_threads, l_def.hasher_threads)),
            property_ptr_t(new main::timeout_t(l.timeout, l_def.timeout)),
            // clang-format on
        };
        return new category_t("main", "main application settings", std::move(props));
    }();

    auto c_relay = [&]() -> category_ptr_t {
        auto &l = config.relay_config;
        auto &l_def = default_config.relay_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new relay::enabled_t(l.enabled, l_def.enabled)),
            property_ptr_t(new relay::discovery_url_t(l.discovery_url, l_def.discovery_url)),
            property_ptr_t(new relay::rx_buff_size_t(l.rx_buff_size, l_def.rx_buff_size)),
            // clang-format on
        };
        return new category_t("relay", "relay network/protocol settings", std::move(props));
    }();

    auto c_upnp = [&]() -> category_ptr_t {
        auto &l = config.upnp_config;
        auto &l_def = default_config.upnp_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new upnp::enabled_t(l.enabled, l_def.enabled)),
            property_ptr_t(new upnp::debug_t(l.debug, l_def.debug)),
            property_ptr_t(new upnp::external_port_t(l.external_port, l_def.external_port)),
            property_ptr_t(new upnp::max_wait_t(l.max_wait, l_def.max_wait)),
            property_ptr_t(new upnp::rx_buff_size_t(l.rx_buff_size, l_def.rx_buff_size)),
            // clang-format on
        };
        return new category_t("upnp", "universal plug and play router settings", std::move(props));
    }();

    auto c_logs = [&]() -> category_ptr_t {
        auto &sinks = config.log_configs;
        auto props = properties_t{};
        for (auto &s : sinks) {
            props.emplace_back(property_ptr_t(new impl::log_sink_t(s.name, s.sinks, s.level)));
        }
        return new category_t("logging", "log sinks & levels", std::move(props));
    }();

    r.push_back(std::move(c_bep));
    r.push_back(std::move(c_db));
    r.push_back(std::move(c_dialer));
    r.push_back(std::move(c_fs));
    r.push_back(std::move(c_gd));
    r.push_back(std::move(c_ld));
    r.push_back(std::move(c_main));
    r.push_back(std::move(c_relay));
    r.push_back(std::move(c_upnp));
    r.push_back(std::move(c_logs));

    return r;
}

auto reflect(const categories_t &categories) -> main_cfg_t {
    main_cfg_t cfg;
    for (auto &c : categories) {
        for (auto &p : c->get_properties()) {
            p->reflect_to(cfg);
        }
    }
    return cfg;
}

} // namespace syncspirit::fltk::config
