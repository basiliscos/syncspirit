#include "new_folder_activity.h"

#include "default_activity.h"
#include "tui_actor.h"
#include "../utils/sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;
using sink_t = syncspirit::utils::sink_t;

new_folder_activity_t::new_folder_activity_t(tui_actor_t &actor_, ui::message::new_folder_notify_t &message) noexcept
    : activity_t{actor_, activity_type_t::NEW_FOLDER}, folder{message.payload.folder}, source{message.payload.source},
      source_index{message.payload.source_index} {}

bool new_folder_activity_t::operator==(const activity_t &other) const noexcept {
    if (type != other.type) {
        return false;
    }
    auto o = (const new_folder_activity_t &)other;
    return source == o.source && folder.id() == o.folder.id();
}

void new_folder_activity_t::display() noexcept {
    auto short_id = source->device_id.get_short();
    auto &label = folder.label();
    auto p = fmt::format("{}{}a{}dd or {}{}i{}gnore folder {}{}{}{} ({}) from {}? ", sink_t::bold, sink_t::white,
                         sink_t::reset,                                     /* add */
                         sink_t::bold, sink_t::white, sink_t::reset,        /* ignore */
                         sink_t::bold, sink_t::green, label, sink_t::reset, /* label */
                         folder.id(),                                       /* id */
                         short_id);
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool new_folder_activity_t::handle(const char key) noexcept {
    switch (sub_activity) {
    case sub_activity_t::main:
        return handle_main(key);
    case sub_activity_t::editing_path:
        return handle_path(key);
    }
    return false;
}

bool new_folder_activity_t::handle_path(const char key) noexcept {
    bool handled = false;
    auto sz = strlen(buff);
    if (key == 0x08 || key == 0x7F) { /* backspace or del */
        if (sz) {
            buff[sz - 1] = 0;
        }
        handled = true;
    } else if (std::isalnum(key) || key == '_' || key == '+' || key == '-' || key == '.') {
        auto sz = strlen(buff);
        if (sz < MAX_PATH_SZ - 1) {
            buff[sz] = key;
            buff[sz + 1] = 0;
        }
        handled = true;
    } else if (key == 0x1B) { /* ESC */
        sub_activity = sub_activity_t::main;
        display();
        return true;
    } else if (key == 0x0A) { /* ENTER */
        if (sz > 0) {
            auto path = std::string(buff, sz);
            actor.create_folder(folder, source, source_index, path);
            actor.discard_activity();
        } else {
            /* ignore, no-op */
            return true;
        }
    }
    if (handled) {
        display_path();
        return true;
    } else {
        return false;
    }

    return false;
}

void new_folder_activity_t::display_path() noexcept {
    auto &label = folder.label();
    auto &id = folder.id();
    auto p =
        fmt::format("path for {}{}{}{} ({}) label: {}", sink_t::bold, sink_t::green, label, sink_t::reset, /* label */
                    id, buff);
    actor.set_prompt(std::string(p.begin(), p.end()));
}

bool new_folder_activity_t::handle_main(const char key) noexcept {
    if (key == 'i') {
        actor.ignore_folder(folder, source);
        actor.discard_activity();
        return true;
    } else if (key == 'a') {
        sub_activity = sub_activity_t::editing_path;
        auto root = actor.app_config_orig.default_location;
        root /= folder.label();
        auto sz = std::min(root.string().size(), MAX_PATH_SZ);
        memcpy(buff, root.c_str(), sz);
        display_path();
    }
    return false;
}
