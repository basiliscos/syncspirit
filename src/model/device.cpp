#include "device.h"

using namespace syncspirit;
using namespace syncspirit::model;

device_t::device_t(config::device_config_t &config) noexcept
    : device_id{device_id_t::from_string(config.id).value()}, name{config.name}, compression{config.compression},
      cert_name{config.cert_name}, static_addresses{config.static_addresses}, introducer{config.introducer},
      auto_accept{config.auto_accept}, paused{config.paused}, skip_introduction_removals{
                                                                  config.skip_introduction_removals} {}

config::device_config_t device_t::serialize() noexcept {
    return config::device_config_t{device_id.get_value(),
                                   name,
                                   compression,
                                   cert_name,
                                   introducer,
                                   auto_accept,
                                   paused,
                                   skip_introduction_removals,
                                   static_addresses,
                                   {}};
}
