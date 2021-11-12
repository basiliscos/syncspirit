#pragma once

#include "../model/device.h"
#include "../net/messages.h"
#include "../config/main.h"

namespace syncspirit::ui {

namespace r = rotor;

namespace payload {

using config_response_t = config::main_t;

struct config_request_t {
    using response_t = config_response_t;
};

struct config_save_response_t {};

struct config_save_request_t {
    using response_t = config_save_response_t;
    config::main_t config;
};

} // namespace payload

namespace message {

using config_request_t = r::request_traits_t<payload::config_request_t>::request::message_t;
using config_response_t = r::request_traits_t<payload::config_request_t>::response::message_t;

using config_save_request_t = r::request_traits_t<payload::config_save_request_t>::request::message_t;
using config_save_response_t = r::request_traits_t<payload::config_save_request_t>::response::message_t;

} // namespace message

} // namespace syncspirit::ui
