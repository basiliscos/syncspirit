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

struct ignore_device_response_t {};

struct ignore_device_request_t {
    using response_t = ignore_device_response_t;
    model::ignored_device_ptr_t device;
};

struct ignore_folder_response_t {};

struct ignore_folder_request_t {
    using response_t = ignore_folder_response_t;
    model::ignored_folder_ptr_t folder;
};

struct update_peer_response_t {};

struct update_peer_request_t {
    using response_t = update_peer_response_t;
    model::device_ptr_t peer;
};

struct new_folder_notify_t {
    proto::Folder folder;
    model::device_ptr_t source;
};

struct create_folder_response_t {};

struct create_folder_request_t {
    using response_t = create_folder_response_t;
    db::Folder folder;
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

using ignore_device_request_t = r::request_traits_t<payload::ignore_device_request_t>::request::message_t;
using ignore_device_response_t = r::request_traits_t<payload::ignore_device_request_t>::response::message_t;

using ignore_folder_request_t = r::request_traits_t<payload::ignore_folder_request_t>::request::message_t;
using ignore_folder_response_t = r::request_traits_t<payload::ignore_folder_request_t>::response::message_t;

using update_peer_request_t = r::request_traits_t<payload::update_peer_request_t>::request::message_t;
using update_peer_response_t = r::request_traits_t<payload::update_peer_request_t>::response::message_t;

} // namespace message

} // namespace syncspirit::ui
