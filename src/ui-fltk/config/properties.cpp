#include "properties.h"

namespace syncspirit::fltk::config {

namespace impl {

positive_integer_t::positive_integer_t(std::string label, std::string explanation, std::string value,
                                       std::string default_value)
    : property_t(std::move(label), std::move(explanation), std::move(value), std::move(default_value),
                 property_kind_t::positive_integer) {}

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

string_t::string_t(std::string label, std::string explanation, std::string value, std::string default_value)
    : property_t(std::move(label), std::move(explanation), std::move(value), std::move(default_value),
                 property_kind_t::text) {}

bool_t::bool_t(bool value, bool default_value, std::string label)
    : property_t(std::move(label), "", std::to_string(value), std::to_string(default_value), property_kind_t::boolean) {
}

error_ptr_t bool_t::validate_value() noexcept {
    native_value = (value == "true") ? 1 : 0;
    return {};
}

} // namespace impl

namespace bep {

blocks_max_requested_t::blocks_max_requested_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("blocks_max_requested", explanation_, std::to_string(value), std::to_string(default_value)) {}

void blocks_max_requested_t::reflect_to(syncspirit::config::main_t &main) {
    main.bep_config.blocks_max_requested = native_value;
}

const char *blocks_max_requested_t::explanation_ = "maximum concurrent block read requests per peer";

blocks_simultaneous_write_t::blocks_simultaneous_write_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("blocks_simultaneous_write", explanation_, std::to_string(value), std::to_string(default_value)) {}

void blocks_simultaneous_write_t::reflect_to(syncspirit::config::main_t &main) {
    main.bep_config.blocks_simultaneous_write = native_value;
}

const char *blocks_simultaneous_write_t::explanation_ = "maximum concurrent block write requests to disk";

connect_timeout_t::connect_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("connect_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void connect_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.connect_timeout = native_value; }

const char *connect_timeout_t::explanation_ = "maximum time for connection, milliseconds";

request_timeout_t::request_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("request_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void request_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.request_timeout = native_value; }

const char *request_timeout_t::explanation_ = "maximum time for request, milliseconds";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, std::to_string(value), std::to_string(default_value)) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.rx_buff_size = native_value; }

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

rx_timeout_t::rx_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void rx_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.rx_timeout = native_value; }

const char *rx_timeout_t::explanation_ = "rx max time, milliseconds";

tx_buff_limit_t::tx_buff_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("tx_buff_limit", explanation_, std::to_string(value), std::to_string(default_value)) {}

void tx_buff_limit_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.tx_buff_limit = native_value; }

const char *tx_buff_limit_t::explanation_ = "preallocated transmit buffer size";

tx_timeout_t::tx_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("tx_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void tx_timeout_t::reflect_to(syncspirit::config::main_t &main) { main.bep_config.tx_timeout = native_value; }

const char *tx_timeout_t::explanation_ = "tx max time, milliseconds";

} // namespace bep

namespace db {

uncommited_threshold_t::uncommited_threshold_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("uncommited_threshold", explanation_, std::to_string(value), std::to_string(default_value)) {}

void uncommited_threshold_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

const char *uncommited_threshold_t::explanation_ = "how many transactions keep in memory before flushing to storage";

upper_limit_t::upper_limit_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("upper_limit", explanation_, std::to_string(value), std::to_string(default_value)) {}

void upper_limit_t::reflect_to(syncspirit::config::main_t &main) {
    main.db_config.uncommitted_threshold = native_value;
}

const char *upper_limit_t::explanation_ = "maximum database size, in bytes";

} // namespace db

namespace dialer {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.dialer_config.enabled = native_value; }

redial_timeout_t::redial_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("redial_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void redial_timeout_t::reflect_to(syncspirit::config::main_t &main) {
    main.dialer_config.redial_timeout = native_value;
}

const char *redial_timeout_t::explanation_ = "how often try to redial to connect offline peers, milliseconds";

} // namespace dialer

namespace fs {

mru_size_t::mru_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("mru_size", explanation_, std::to_string(value), std::to_string(default_value)) {}

void mru_size_t::reflect_to(syncspirit::config::main_t &main) { main.fs_config.mru_size = native_value; }

const char *mru_size_t::explanation_ = "maximum amount of cached/opened files";

temporally_timeout_t::temporally_timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("temporally_timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void temporally_timeout_t::reflect_to(syncspirit::config::main_t &main) {
    main.fs_config.temporally_timeout = native_value;
}

const char *temporally_timeout_t::explanation_ = "remove incomplete file after this amount of seconds";

} // namespace fs

namespace global_discovery {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.global_announce_config.enabled = native_value; }

announce_url_t::announce_url_t(std::string value, std::string default_value)
    : parent_t("announce_url", explanation_, std::move(value), std::move(default_value)) {}

void announce_url_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.announce_url = utils::parse(value).value();
}

const char *announce_url_t::explanation_ = "url of syncthing/private global announce server";

cert_file_t::cert_file_t(std::string value, std::string default_value)
    : parent_t("cert_file", explanation_, std::move(value), std::move(default_value)) {}

void cert_file_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.cert_file = utils::parse(value).value();
}

const char *cert_file_t::explanation_ = "this device certificate location";

device_id_t::device_id_t(std::string value, std::string default_value)
    : parent_t("device_id", explanation_, std::move(value), std::move(default_value)) {}

void device_id_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.device_id = utils::parse(value).value();
}

const char *device_id_t::explanation_ = "device_id of global/private discovery server";

key_file_t::key_file_t(std::string value, std::string default_value)
    : parent_t("key_file", explanation_, std::move(value), std::move(default_value)) {}

void key_file_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.key_file = utils::parse(value).value();
}

const char *key_file_t::explanation_ = "this device key location";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, std::to_string(value), std::to_string(default_value)) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.rx_buff_size = native_value;
}

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

timeout_t::timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void timeout_t::reflect_to(syncspirit::config::main_t &main) { main.global_announce_config.timeout = native_value; }

const char *timeout_t::explanation_ = "max request/response time, milliseconds";

} // namespace global_discovery

namespace local_discovery {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.enabled = native_value; }

frequency_t::frequency_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("frequency", explanation_, std::to_string(value), std::to_string(default_value)) {}

void frequency_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.frequency = native_value; }

const char *frequency_t::explanation_ = "how often send announcements in LAN, in milliseconds, milliseconds";

port_t::port_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("port", explanation_, std::to_string(value), std::to_string(default_value)) {}

void port_t::reflect_to(syncspirit::config::main_t &main) { main.local_announce_config.port = native_value; }

const char *port_t::explanation_ = "upd port used for announcement (should be the same as in syncthing)";

} // namespace local_discovery

namespace main {

default_location_t::default_location_t(std::string value, std::string default_value)
    : parent_t("default_location", explanation_, std::move(value), std::move(default_value)) {}

void default_location_t::reflect_to(syncspirit::config::main_t &main) { main.default_location = value; }

const char *default_location_t::explanation_ = "where folders are created by default";

device_name_t::device_name_t(std::string value, std::string default_value)
    : parent_t("device_name", explanation_, std::move(value), std::move(default_value)) {}

void device_name_t::reflect_to(syncspirit::config::main_t &main) { main.device_name = value; }

const char *device_name_t::explanation_ = "this device name";

hasher_threads_t::hasher_threads_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("hasher_threads", explanation_, std::to_string(value), std::to_string(default_value)) {}

void hasher_threads_t::reflect_to(syncspirit::config::main_t &main) { main.hasher_threads = native_value; }

const char *hasher_threads_t::explanation_ = "amount cpu cores used for hashing";

timeout_t::timeout_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("timeout", explanation_, std::to_string(value), std::to_string(default_value)) {}

void timeout_t::reflect_to(syncspirit::config::main_t &main) { main.timeout = native_value; }

const char *timeout_t::explanation_ = "main actors timeout, milliseconds";

} // namespace main

namespace relay {

void enabled_t::reflect_to(syncspirit::config::main_t &main) { main.relay_config.enabled = native_value; }

discovery_url_t::discovery_url_t(std::string value, std::string default_value)
    : parent_t("discovery_url", explanation_, std::move(value), std::move(default_value)) {}

void discovery_url_t::reflect_to(syncspirit::config::main_t &main) {
    main.relay_config.discovery_url = utils::parse(value).value();
}

const char *discovery_url_t::explanation_ = "here pick the list of relay servers pool";

rx_buff_size_t::rx_buff_size_t(std::uint64_t value, std::uint64_t default_value)
    : parent_t("rx_buff_size", explanation_, std::to_string(value), std::to_string(default_value)) {}

void rx_buff_size_t::reflect_to(syncspirit::config::main_t &main) {
    main.global_announce_config.rx_buff_size = native_value;
}

const char *rx_buff_size_t::explanation_ = "preallocated receive buffer size, bytes";

} // namespace relay

} // namespace syncspirit::fltk::config
