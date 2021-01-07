#pragma once

#include "../model/device.h"
#include "../net/messages.h"
#include "../config/main.h"

namespace syncspirit::ui {

namespace r = rotor;

namespace payload {

struct discovery_notification_t {
    using message_ptr_t = r::intrusive_ptr_t<net::message::discovery_notify_t>;

    message_ptr_t net_message;
};

struct auth_notification_t {
    using message_ptr_t = r::intrusive_ptr_t<net::message::auth_request_t>;

    message_ptr_t net_message;
};

using config_response_t = config::main_t;

struct config_request_t {
    using response_t = config_response_t;
};

struct config_save_response_t {};

struct config_save_request_t {
    using response_t = config_save_response_t;
    config::main_t config;
};

struct new_folder_notify_t {
    proto::Folder folder;
    model::device_ptr_t source;
};

struct create_folder_response_t {
    config::main_t config;
};

struct create_folder_request_t {
    using response_t = create_folder_response_t;
    config::folder_config_t folder;
    model::device_ptr_t source;
};

} // namespace payload

namespace message {

using discovery_notify_t = r::message_t<payload::discovery_notification_t>;
using auth_notify_t = r::message_t<payload::auth_notification_t>;
using new_folder_notify_t = r::message_t<payload::new_folder_notify_t>;

using config_request_t = r::request_traits_t<payload::config_request_t>::request::message_t;
using config_response_t = r::request_traits_t<payload::config_request_t>::response::message_t;

using config_save_request_t = r::request_traits_t<payload::config_save_request_t>::request::message_t;
using config_save_response_t = r::request_traits_t<payload::config_save_request_t>::response::message_t;

using create_folder_request_t = r::request_traits_t<payload::create_folder_request_t>::request::message_t;
using create_folder_response_t = r::request_traits_t<payload::create_folder_request_t>::response::message_t;

} // namespace message

} // namespace syncspirit::ui
