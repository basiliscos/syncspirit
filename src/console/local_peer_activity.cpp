#include "local_peer_activity.h"

#include "default_activity.h"
#include "tui_actor.h"
#include "sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;

local_peer_activity_t::local_peer_activity_t(tui_actor_t &actor_, activity_type_t type_,
                                             ui::message::discovery_notify_t &message_) noexcept
    : activity_t{actor_, type_}, message{message_.payload.net_message}, sub_activity{sub_activity_t::main} {
    auto &payload = message->payload;
    address = payload.peer_endpoint.address().to_string();
    short_id = payload.device_id.get_short();
}

bool local_peer_activity_t::handle(const char key) noexcept {
    switch (sub_activity) {
    case sub_activity_t::main:
        return handle_main(key);
    case sub_activity_t::editing_name:
        return handle_label(key);
    }
    return false;
}

bool local_peer_activity_t::handle_main(const char key) noexcept {
    if (key == 'i') {
        actor.ignore_device(message->payload.device_id);
        actor.discard_activity();
        return true;
    } else if (key == 'a') {
        sub_activity = sub_activity_t::editing_name;
        if (strlen(buff) == 0) {
            memcpy(buff, address.data(), address.size());
        }
        display_label();
    }
    return false;
}

bool local_peer_activity_t::handle_label(const char key) noexcept {
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
            auto &devices = actor.app_config.devices;
            auto &id = message->payload.device_id.get_value();
            auto new_device =
                config::device_config_t{id, buff, config::compression_t::meta, {}, false, false, false, false, {}};
            devices.emplace(id, std::move(new_device));
            actor.save_config();
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

void local_peer_activity_t::display() noexcept { display_menu(); }

void local_peer_activity_t::display_menu() noexcept {
    auto p = fmt::format("{}{}a{}dd or {}{}i{}gnore device {}{}{}{} ({})? ", sink_t::bold, sink_t::white,
                         sink_t::reset,                                        /* add */
                         sink_t::bold, sink_t::white, sink_t::reset,           /* ignore */
                         sink_t::bold, sink_t::green, short_id, sink_t::reset, /* device */
                         address);                                             /* IP */
    actor.set_prompt(std::string(p.begin(), p.end()));
}

void local_peer_activity_t::display_label() noexcept {
    auto p = fmt::format("peer {} label: {}{}{}{}", address,              /* peer address */
                         sink_t::bold, sink_t::green, buff, sink_t::reset /* label */
    );
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool local_peer_activity_t::operator==(const activity_t &other) const noexcept {
    if (type != other.type) {
        return false;
    }
    auto o = (const local_peer_activity_t &)other;
    return message->payload.device_id == o.message->payload.device_id;
}
