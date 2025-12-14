// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "category.h"

#include "properties.h"

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
            property_ptr_t(new bep::advances_per_iteration_t(bep.advances_per_iteration, bep_def.advances_per_iteration)),
            property_ptr_t(new bep::ping_timeout_t(bep.ping_timeout, bep_def.ping_timeout)),
            property_ptr_t(new bep::rx_buff_size_t(bep.rx_buff_size, bep_def.rx_buff_size)),
            property_ptr_t(new bep::tx_buff_limit_t(bep.tx_buff_limit, bep_def.tx_buff_limit)),
            property_ptr_t(new bep::stats_interval_t(bep.stats_interval, bep_def.stats_interval)),
            // clang-format on
        };
        return new category_t("bep", "BEP protocol/network settings", std::move(props));
    }();

    auto c_db = [&]() -> category_ptr_t {
        auto &db = config.db_config;
        auto &db_def = default_config.db_config;
        auto props = properties_t{
            // clang-format off
            property_ptr_t(new db::max_blocks_per_diff_t(db.max_blocks_per_diff, db_def.max_blocks_per_diff)),
            property_ptr_t(new db::max_files_per_diff_t(db.max_files_per_diff, db_def.max_files_per_diff)),
            property_ptr_t(new db::uncommitted_threshold_t(db.uncommitted_threshold, db_def.uncommitted_threshold)),
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
            property_ptr_t(new dialer::skip_discovers_t(d.skip_discovers, d_def.skip_discovers)),
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
            property_ptr_t(new fs::bytes_scan_iteration_limit_t(f.bytes_scan_iteration_limit, f_def.bytes_scan_iteration_limit)),
            property_ptr_t(new fs::files_scan_iteration_limit_t(f.files_scan_iteration_limit, f_def.files_scan_iteration_limit)),
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
            property_ptr_t(new global_discovery::debug_t(g.debug, g_def.debug)),
            property_ptr_t(new global_discovery::announce_url_t(g.announce_url->buffer(), g_def.announce_url->buffer())),
            property_ptr_t(new global_discovery::lookup_url_t(g.lookup_url->buffer(), g_def.lookup_url->buffer())),
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
            property_ptr_t(new main::default_location_t(l.default_location, l_def.default_location)),
            property_ptr_t(new main::cert_file_t(l.cert_file, l_def.cert_file)),
            property_ptr_t(new main::key_file_t(l.key_file, l_def.key_file)),
            property_ptr_t(new main::root_ca_file(l.root_ca_file, l_def.root_ca_file)),
            property_ptr_t(new main::device_name_t(l.device_name, l_def.device_name)),
            property_ptr_t(new main::hasher_threads_t(l.hasher_threads, l_def.hasher_threads)),
            property_ptr_t(new main::poll_timeout_t(l.poll_timeout, l_def.poll_timeout)),
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
            property_ptr_t(new relay::debug_t(l.enabled, l_def.enabled)),
            property_ptr_t(new relay::discovery_url_t(l.discovery_url->buffer(), l_def.discovery_url->buffer())),
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

auto reflect(const categories_t &categories, const main_cfg_t &default_config) -> main_cfg_t {
    main_cfg_t cfg = default_config;
    for (auto &c : categories) {
        for (auto &p : c->get_properties()) {
            p->reflect_to(cfg);
        }
    }
    return cfg;
}

bool is_valid(const categories_t &categories) {
    for (auto &c : categories) {
        for (auto &p : c->get_properties()) {
            if (auto &err = p->validate(); err) {
                return false;
            }
        }
    }
    return true;
}

} // namespace syncspirit::fltk::config
