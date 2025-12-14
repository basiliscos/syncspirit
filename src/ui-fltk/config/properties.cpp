// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "properties.h"
#include "utils/log.h"
#include "model/device_id.h"
#include <charconv>
#include <boost/nowide/convert.hpp>
#include <filesystem>
#include <fmt/ranges.h>

namespace syncspirit::fltk::config {

namespace impl {

positive_integer_t::positive_integer_t(std::string label, std::string explanation, uint64_t value,
                                       uint64_t default_value)
    : property_t(std::move(label), std::move(explanation), std::to_string(value), std::to_string(default_value),
                 property_kind_t::positive_integer),
      native_value{value} {}

error_ptr_t positive_integer_t::validate_value() noexcept {
    std::uint64_t r;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), r);

    if (ec == std::errc::invalid_argument) {
        return error_ptr_t(new std::string("not a number"));
    } else if (ec == std::errc::result_out_of_range) {
        return error_ptr_t(new std::string("too large number"));
    }
    assert(ec == std::errc());

    if (r <= 0) {
        return error_ptr_t(new std::string("not a positive number"));
    }

    // all ok
    native_value = static_cast<std::uint64_t>(r);
    return {};
}

integer_t::integer_t(std::string label, std::string explanation, int64_t value, int64_t default_value)
    : property_t(std::move(label), std::move(explanation), std::to_string(value), std::to_string(default_value),
                 property_kind_t::positive_integer),
      native_value{value} {}

error_ptr_t integer_t::validate_value() noexcept {
    std::int64_t r;
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), r);

    if (ec == std::errc::invalid_argument) {
        return error_ptr_t(new std::string("not a number"));
    } else if (ec == std::errc::result_out_of_range) {
        return error_ptr_t(new std::string("too large number"));
    }
    assert(ec == std::errc());

    // all ok
    native_value = r;
    return {};
}

string_t::string_t(std::string label, std::string explanation, std::string value, std::string default_value)
    : property_t(std::move(label), std::move(explanation), std::move(value), std::move(default_value),
                 property_kind_t::text) {}

error_ptr_t url_t::validate_value() noexcept {
    auto new_url = utils::parse(value);
    if (!new_url) {
        return error_ptr_t(new std::string("invalid url"));
    }
    auto original = utils::parse(default_value);
    if (new_url->scheme() != original->scheme()) {
        return error_ptr_t(new std::string("invalid protocol"));
    }
    return {};
}

static std::string _to_str(const bfs::path &p) { return boost::nowide::narrow(p.wstring()); }

path_t::path_t(std::string label, std::string explanation, const bfs::path &value, const bfs::path &default_value,
               property_kind_t kind)
    : property_t(std::move(label), std::move(explanation), std::move(_to_str(value)), std::move(_to_str(default_value)),
                 kind) {}

bfs::path path_t::convert() noexcept { return bfs::path(boost::nowide::widen(value)); }

bool_t::bool_t(bool value, bool default_value, std::string label)
    : property_t(std::move(label), "", value ? "true" : "", default_value ? "true" : "", property_kind_t::boolean),
      native_value(value ? 1 : 0) {}

error_ptr_t bool_t::validate_value() noexcept {
    native_value = (value == "true") ? 1 : 0;
    return {};
}

static std::string join(const std::vector<std::string> &sinks) { return fmt::format("{}", fmt::join(sinks, ", ")); }

log_sink_t::log_sink_t(std::string name, std::vector<std::string> sinks_, spdlog::level::level_enum level_)
    : property_t(std::move(name), std::string(utils::get_level_string(level_)), join(sinks_), join(sinks_),
                 property_kind_t::log_sink),
      sinks(std::move(sinks_)), level{level_} {}

void log_sink_t::reflect_to(syncspirit::config::main_t &main) {
    main.log_configs.clear();
    main.log_configs.push_back(syncspirit::config::log_config_t{
        label,
        level,
        sinks,
    });
}

} // namespace impl

namespace bep {

blocks_max_requested_t::blocks_max_requested_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("blocks_max_requested", explanation_, value, default_value) {}

void blocks_max_requested_t::reflect_to(syncspirit::config::main_t &main) {
    main.bep_config.blocks_max_requested = native_value;
}

const char *blocks_max_requested_t::explanation_ = "maximum concurrent block read requests per peer";

blocks_simultaneous_write_t::blocks_simultaneous_write_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("blocks_simultaneous_write", explanation_, value, default_value) {}

void blocks_simultaneous_write_t::reflect_to(syncspirit::config::main_t &main) {
    main.bep_config.blocks_simultaneous_write = native_value;
}

const char *blocks_simultaneous_write_t::explanation_ = "maximum concurrent block write requests to disk";

connect_timeout_t::connect_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("connect_timeout", explanation_, value, default_value) {}

void connect_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.connect_timeout = native_value; }

const char *connect_timeout_t::explanation_ = "maximum time for connection, milliseconds";

advances_per_iteration_t::advances_per_iteration_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("advances_per_iteration", explanation_, value, default_value) {}

void advances_per_iteration_t::reflect_to(syncspirit::config::main_t &main) {
    main.bep_config.advances_per_iteration = native_value;
}

const char *advances_per_iteration_t::explanation_ = "maximum amount of file metadata advances per iteration";

ping_timeout_t::ping_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("ping_timeout", explanation_, value, default_value) {}

void ping_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.ping_timeout = native_value; }

const char *ping_timeout_t::explanation_ = "maximum interval between pings, milliseconds";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, value, default_value) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.rx_buff_size = native_value; }

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

tx_buff_limit_t::tx_buff_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("tx_buff_limit", explanation_, value, default_value) {}

void tx_buff_limit_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.tx_buff_limit = native_value; }

const char *tx_buff_limit_t::explanation_ = "preallocated transmit buffer size";

stats_interval_t::stats_interval_t(std::int64_t value, std::int64_t default_value)
    : parent_t("tx_timeout", explanation_, value, default_value) {}

void stats_interval_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.stats_interval = native_value; }

const char *stats_interval_t::explanation_ = "min delay before gathering I/O stats, milliseconds";

} // namespace bep

namespace db {

max_blocks_per_diff_t::max_blocks_per_diff_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("max_blocks_per_diff", explanation_, value, default_value) {}

void max_blocks_per_diff_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.max_blocks_per_diff = native_value;
}

const char *max_blocks_per_diff_t::explanation_ =
    "maximum number of blocks per single diff (to display progress in UI)";

max_files_per_diff_t::max_files_per_diff_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("max_files_per_diff", explanation_, value, default_value) {}

void max_files_per_diff_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.max_files_per_diff = native_value;
}

const char *max_files_per_diff_t::explanation_ = "maximum number of files per single diff (to display progress in UI)";

uncommitted_threshold_t::uncommitted_threshold_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("uncommitted_threshold", explanation_, value, default_value) {}

void uncommitted_threshold_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

const char *uncommitted_threshold_t::explanation_ = "how many transactions keep in memory before flushing to storage";

upper_limit_t::upper_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("upper_limit", explanation_, value, default_value) {}

void upper_limit_t::reflect_to(syncspirit::config::main_t &main) { main.db_config.upper_limit = native_value; }

const char *upper_limit_t::explanation_ = "maximum database size, in bytes";

} // namespace db

namespace dialer {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.dialer_config.enabled = native_value; }

redial_timeout_t::redial_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("redial_timeout", explanation_, value, default_value) {}

void redial_timeout_t::reflect_to(syncspirit::config::main_t &main) {
    main.dialer_config.redial_timeout = native_value;
}

const char *redial_timeout_t::explanation_ = "how often try to redial to connect offline peers, milliseconds";

skip_discovers_t::skip_discovers_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("skip_discovers", explanation_, value, default_value) {}

void skip_discovers_t::reflect_to(syncspirit::config::main_t &main) {
    main.dialer_config.skip_discovers = native_value;
}

const char *skip_discovers_t::explanation_ = "when peer addresses are known, how many times skip rediscovering them";

} // namespace dialer

namespace fs {

mru_size_t::mru_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("mru_size", explanation_, value, default_value) {}

void mru_size_t::reflect_to(syncspirit::config::main_t &main) { main.fs_config.mru_size = native_value; }

const char *mru_size_t::explanation_ = "maximum amount of cached/opened files";

temporally_timeout_t::temporally_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("temporally_timeout", explanation_, value, default_value) {}

void temporally_timeout_t::reflect_to(syncspirit::config::main_t &main) {
    main.fs_config.temporally_timeout = native_value;
}

const char *temporally_timeout_t::explanation_ = "remove incomplete file after this amount of seconds";

bytes_scan_iteration_limit_t::bytes_scan_iteration_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("bytes_scan_iteration_limit", explanation_, value, default_value) {}

void bytes_scan_iteration_limit_t::reflect_to(syncspirit::config::main_t &main) {
    main.fs_config.bytes_scan_iteration_limit = native_value;
}

const char *bytes_scan_iteration_limit_t::explanation_ = "max number of bytes before emitting scan events";

files_scan_iteration_limit_t::files_scan_iteration_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("files_scan_iteration_limit", explanation_, value, default_value) {}

void files_scan_iteration_limit_t::reflect_to(syncspirit::config::main_t &main) {
    main.fs_config.files_scan_iteration_limit = native_value;
}

const char *files_scan_iteration_limit_t::explanation_ = "max number processed files before emitting scan events";

} // namespace fs

namespace global_discovery {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.global_announce_config.enabled = native_value; }

debug_t::debug_t(bool value, bool default_value) : parent_t(value, default_value, "debug") {}

void debug_t::reflect_to(syncspirit::config::main_t &main) { main.global_announce_config.debug = native_value; }

announce_url_t::announce_url_t(std::string value, std::string default_value)
    : parent_t("announce_url", explanation_, std::move(value), std::move(default_value)) {}

void announce_url_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.announce_url = utils::parse(value);
}

const char *announce_url_t::explanation_ = "url of syncthing/private announce server";

lookup_url_t::lookup_url_t(std::string value, std::string default_value)
    : parent_t("lookup_url", explanation_, std::move(value), std::move(default_value)) {}

void lookup_url_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.lookup_url = utils::parse(value);
}

const char *lookup_url_t::explanation_ = "url of syncthing/private lookup/discovery server";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, value, default_value) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.rx_buff_size = native_value;
}

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

timeout_t::timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("timeout", explanation_, value, default_value) {}

void timeout_t::reflect_to(syncspirit::config::main_t &main) { main.global_announce_config.timeout = native_value; }

const char *timeout_t::explanation_ = "max request/response time, milliseconds";

} // namespace global_discovery

namespace local_discovery {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.enabled = native_value; }

frequency_t::frequency_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("frequency", explanation_, value, default_value) {}

void frequency_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.frequency = native_value; }

const char *frequency_t::explanation_ = "how often send announcements in LAN, in milliseconds, milliseconds";

port_t::port_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("port", explanation_, value, default_value) {}

void port_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.port = native_value; }

const char *port_t::explanation_ = "upd port used for announcement (should be the same as in syncthing)";

} // namespace local_discovery

namespace main {

default_location_t::default_location_t(const bfs::path &value, const bfs::path &default_value)
    : parent_t("default_location", explanation_, value, default_value, property_kind_t::directory) {}

void default_location_t::reflect_to(syncspirit::config::main_t &main) { main.default_location = convert(); }

const char *default_location_t::explanation_ = "where folders are created by default";

cert_file_t::cert_file_t(const bfs::path &value, const bfs::path &default_value)
    : parent_t("cert_file", explanation_, value, default_value) {}

void cert_file_t::reflect_to(syncspirit::config::main_t &main) { main.cert_file = convert(); }

const char *cert_file_t::explanation_ = "this device certificate location";

key_file_t::key_file_t(const bfs::path &value, const bfs::path &default_value)
    : parent_t("key_file", explanation_, value, default_value) {}

void key_file_t::reflect_to(syncspirit::config::main_t &main) { main.key_file = convert(); }

const char *key_file_t::explanation_ = "this device key location";

root_ca_file::root_ca_file(const bfs::path &value, const bfs::path &default_value)
    : parent_t("root_ca_file", explanation_, value, default_value, property_kind_t::file) {}

void root_ca_file::reflect_to(syncspirit::config::main_t &main) { main.root_ca_file = convert(); }

const char *root_ca_file::explanation_ = "root certificate authority (ca) file (PEM format)";

device_name_t::device_name_t(std::string value, std::string default_value)
    : parent_t("device_name", explanation_, std::move(value), std::move(default_value)) {}

void device_name_t::reflect_to(syncspirit::config::main_t &main) { main.device_name = value; }

const char *device_name_t::explanation_ = "this device name";

hasher_threads_t::hasher_threads_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("hasher_threads", explanation_, value, default_value) {}

void hasher_threads_t::reflect_to(syncspirit::config::main_t &main) { main.hasher_threads = native_value; }

const char *hasher_threads_t::explanation_ = "amount cpu cores used for hashing";

poll_timeout_t::poll_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("poll_timeout", explanation_, value, default_value) {}

void poll_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.poll_timeout = native_value; }

const char *poll_timeout_t::explanation_ = "amount of microseconds of spin-lock polling (0 to save CPU)";

timeout_t::timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("timeout", explanation_, value, default_value) {}

void timeout_t::reflect_to(syncspirit::config::main_t &main) { main.timeout = native_value; }

const char *timeout_t::explanation_ = "main actors timeout, milliseconds";

} // namespace main

namespace relay {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.relay_config.enabled = native_value; }

debug_t::debug_t(bool value, bool default_value) : parent_t(value, default_value, "debug") {}

void debug_t::reflect_to(syncspirit::config::main_t &main) { main.relay_config.debug = native_value; }

discovery_url_t::discovery_url_t(std::string value, std::string default_value)
    : parent_t("discovery_url", explanation_, std::move(value), std::move(default_value)) {}

void discovery_url_t::reflect_to(syncspirit::config::main_t &main) {
    main.relay_config.discovery_url = utils::parse(value);
}

const char *discovery_url_t::explanation_ = "where pick the list of relay servers pool";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, value, default_value) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) { main.relay_config.rx_buff_size = native_value; }

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

} // namespace relay

namespace upnp {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.upnp_config.enabled = native_value; }

debug_t::debug_t(bool value, bool default_value) : parent_t(value, default_value, "debug") {}

void debug_t::reflect_to(syncspirit::config::main_t &main) { main.upnp_config.debug = native_value; }

external_port_t::external_port_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("external_port", explanation_, value, default_value) {}

void external_port_t::reflect_to(syncspirit::config::main_t &main) { main.upnp_config.external_port = native_value; }

const char *external_port_t::explanation_ = "external (router) port for communication, opened on router";

max_wait_t::max_wait_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("max_wait", explanation_, value, default_value) {}

void max_wait_t::reflect_to(syncspirit::config::main_t &main) { main.upnp_config.max_wait = native_value; }

const char *max_wait_t::explanation_ = "router response max wait time, seconds";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, value, default_value) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) { main.upnp_config.rx_buff_size = native_value; }

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

} // namespace upnp

} // namespace syncspirit::fltk::config
