#include "device.h"

using namespace syncspirit::model;

device_t::device_t(config::device_config_t &config) noexcept
    : device_id{device_id_t::from_string(config.id).value()}, name{config.name},
      static_addresses{config.static_addresses}, intoducer{config.introducer},
      auto_accept{config.auto_accept}, paused{config.paused} {}
