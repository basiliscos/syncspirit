#include "config_activity.h"
#include "tui_actor.h"
#include "sink.h"
#include <fmt/fmt.h>
#include <cctype>

using namespace syncspirit::console;

config_activity_t::config_activity_t(tui_actor_t &actor_, activity_type_t type_, config::configuration_t &config_,
                                     config::configuration_t &config_orig_) noexcept
    : activity_t{actor_, type_}, config{config_}, config_orig{config_orig_}, sub_activity{sub_activity_t::main} {}

void config_activity_t::display() noexcept { display_menu(); }

void config_activity_t::forget() noexcept { actor.discard_activity(); }

void config_activity_t::display_menu() noexcept {
    auto letter = [](char c) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, std::string_view(&c, 1), sink_t::reset);
    };
    auto key = [](const char *val) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, val, sink_t::reset);
    };

    auto matches = config == config_orig;
    auto needs_save = matches ? ' ' : '*';
    auto changes = std::string_view(&needs_save, 1);

    auto p = fmt::format(
        "{}{}{} [{}] - devices, [{}] - folders, [{}] - device name,  [{}] - timeout, [{}] - save, [{}] - back > ",
        sink_t::yellow_bold, changes, sink_t::reset, letter('d'), letter('f'), letter('n'), letter('t'), letter('s'),
        key("ESC"));
    actor.set_prompt(std::string(p.begin(), p.end()));
}

void config_activity_t::display_device() noexcept {
    auto p = fmt::format("device name > {}{}{}{}", sink_t::bold, sink_t::white, buff, sink_t::reset);
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool config_activity_t::handle(const char key) noexcept {
    switch (sub_activity) {
    case sub_activity_t::main:
        return handle_main(key);
    case sub_activity_t::editing_device:
        return handle_device(key);
    }
    return false;
}

bool config_activity_t::handle_main(const char key) noexcept {
    if (key == 'n') {
        sub_activity = sub_activity_t::editing_device;
        size_t device_sz = std::min(MAX_DEVICE_NAME - 1, config.device_name.size());
        memcpy(buff, config.device_name.data(), device_sz);
        display_device();
        return true;
    } else if (key == 's') {
        if (!(config == config_orig)) {
            actor.save_config();
            forget();
        }
    }
    return false;
}

bool config_activity_t::handle_device(const char key) noexcept {
    bool handled = false;
    auto sz = strlen(buff);
    if (key == 0x08 || key == 0x7F) { /* backspace or del */
        if (sz) {
            buff[sz - 1] = 0;
        }
        handled = true;
    } else if (std::isalnum(key) || key == '_' || key == '+' || key == '-') {
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
        sub_activity = sub_activity_t::main;
        config.device_name = std::string(buff, sz);
        display_menu();
        return true;
    }
    if (handled) {
        display_device();
        return true;
    } else {
        return false;
    }
}
