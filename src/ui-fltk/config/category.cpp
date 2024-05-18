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
        auto props = properties_t{};

        return new category_t("fs", "filesystem settings", std::move(props));
    }();

    auto c_gd = [&]() -> category_ptr_t {
        auto props = properties_t{};

        return new category_t("global_discovery", "global peer discovery settings", std::move(props));
    }();

    auto c_ld = [&]() -> category_ptr_t {
        auto props = properties_t{};

        return new category_t("local_discovery", "LAN peer discovery settings", std::move(props));
    }();

    auto c_main = [&]() -> category_ptr_t {
        auto props = properties_t{};

        return new category_t("main", "main application settings", std::move(props));
    }();

    auto c_relay = [&]() -> category_ptr_t {
        auto props = properties_t{};

        return new category_t("relay", "relay network/protocol settings", std::move(props));
    }();

    auto c_upnp = [&]() -> category_ptr_t {
        auto props = properties_t{};

        return new category_t("upnp", "universal plug and play router settings", std::move(props));
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

    return r;
}

} // namespace syncspirit::fltk::config
