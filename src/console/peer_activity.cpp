#include "peer_activity.h"

#include "default_activity.h"
#include "tui_actor.h"
#include "../utils/sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;
using sink_t = syncspirit::utils::sink_t;

peer_activity_t::peer_activity_t(tui_actor_t &actor_, ui::message::discovery_notify_t &message) noexcept
    : activity_t{actor_, activity_type_t::PEER}, sub_activity{sub_activity_t::main} {
    auto &payload = message.payload.net_message->payload;
    peer_name = peer_details = fmt::format("{}", payload.peer_endpoint);
    device_id = payload.device_id;
}

peer_activity_t::peer_activity_t(tui_actor_t &actor_, ui::message::auth_notify_t &message) noexcept
    : activity_t{actor_, activity_type_t::PEER}, sub_activity{sub_activity_t::main} {
    auto &payload = message.payload.net_message->payload.request_payload;
    auto &hello = payload->hello;
    peer_details = fmt::format("{}, {}/{}", payload->endpoint, hello.client_name(), hello.client_version());
    device_id = payload->peer_device_id;
    cert_name = payload->cert_name;
    peer_name = hello.device_name();
}

bool peer_activity_t::handle(const char key) noexcept {
    switch (sub_activity) {
    case sub_activity_t::main:
        return handle_main(key);
    case sub_activity_t::editing_name:
        return handle_label(key);
    }
    return false;
}

bool peer_activity_t::handle_main(const char key) noexcept {
    if (key == 'i') {
        actor.ignore_device(device_id);
        actor.discard_activity();
        return true;
    } else if (key == 'a') {
        sub_activity = sub_activity_t::editing_name;
        if (strlen(buff) == 0) {
            memcpy(buff, peer_name.data(), peer_name.size());
        }
        display_label();
    }
    return false;
}

bool peer_activity_t::handle_label(const char key) noexcept {
    bool handled = false;
    auto sz = strlen(buff);
    if (key == 0x08 || key == 0x7F) { /* backspace or del */
        if (sz) {
            buff[sz - 1] = 0;
        }
        handled = true;
    } else if (std::isalnum(key) || key == '_' || key == '+' || key == '-' || key == '.') {
        auto sz = strlen(buff);
        if (sz < MAX_DEVICE_NAME - 1) {
            buff[sz] = key;
            buff[sz + 1] = 0;
        }
        handled = true;
    } else if (key == 0x1B) { /* ESC */
        sub_activity = sub_activity_t::main;
        display_menu();
        return true;
    } else if (key == 0x0A) { /* ENTER */
        if (sz > 0) {
            auto &id = device_id.get_sha256();
            db::Device db_dev;
            db_dev.set_id(id);
            db_dev.set_name(buff);
            if (cert_name) {
                db_dev.set_cert_name(cert_name.value());
            }
            auto device = model::device_ptr_t(new model::device_t(db_dev));
            actor.update_device(device);
            actor.discard_activity();
        } else {
            /* ignore, no-op */
            return true;
        }
    }
    if (handled) {
        display_label();
        return true;
    } else {
        return false;
    }
}

bool peer_activity_t::locked() noexcept { return sub_activity != sub_activity_t::main; }

void peer_activity_t::display() noexcept { display_menu(); }

void peer_activity_t::display_menu() noexcept {
    auto short_id = device_id.get_short();
    auto p = fmt::format("{}{}a{}dd or {}{}i{}gnore device {}{}{}{} ({})? ", sink_t::bold, sink_t::white,
                         sink_t::reset,                                        /* add */
                         sink_t::bold, sink_t::white, sink_t::reset,           /* ignore */
                         sink_t::bold, sink_t::green, short_id, sink_t::reset, /* device */
                         peer_details);                                        /* IP */
    actor.set_prompt(std::string(p.begin(), p.end()));
}

void peer_activity_t::display_label() noexcept {
    auto p = fmt::format("peer {} label: {}{}{}{}", peer_details,         /* peer address */
                         sink_t::bold, sink_t::green, buff, sink_t::reset /* label */
    );
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool peer_activity_t::operator==(const activity_t &other) const noexcept {
    if (type != other.type) {
        return false;
    }
    auto o = (const peer_activity_t &)other;
    return device_id == o.device_id;
}
