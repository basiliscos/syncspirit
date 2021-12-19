#pragma once
#include <cstdint>
#include <boost/outcome.hpp>
#include "main.h"

namespace syncspirit::config {

namespace outcome = boost::outcome_v2;

// comparators

bool operator==(const bep_config_t &lhs, const bep_config_t &rhs) noexcept;
bool operator==(const dialer_config_t &lhs, const dialer_config_t &rhs) noexcept;
bool operator==(const fs_config_t &lhs, const fs_config_t &rhs) noexcept;
bool operator==(const global_announce_config_t &lhs, const global_announce_config_t &rhs) noexcept;
bool operator==(const local_announce_config_t &lhs, const local_announce_config_t &rhs) noexcept;
bool operator==(const main_t &lhs, const main_t &rhs) noexcept;
bool operator==(const upnp_config_t &lhs, const upnp_config_t &rhs) noexcept;

// misc

using config_result_t = outcome::outcome<main_t, std::string>;

config_result_t get_config(std::istream &config, const boost::filesystem::path &config_path);

outcome::result<main_t> generate_config(const boost::filesystem::path &config_path);

outcome::result<void> serialize(const main_t cfg, std::ostream &out) noexcept;

} // namespace syncspirit::config
