#include "new_folder_activity.h"

#include "default_activity.h"
#include "tui_actor.h"
#include "sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;
new_folder_activity_t::new_folder_activity_t(tui_actor_t &actor_, ui::message::new_folder_notify_t &message) noexcept
    : activity_t{actor_, activity_type_t::NEW_FOLDER}, folder{message.payload.folder}, source{message.payload.source} {}

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
    if (key == 'i') {
        actor.ignore_folder(folder, source);
        actor.discard_activity();
        return true;
    } else if (key == 'a') {
        actor.create_folder(folder, source);
        actor.discard_activity();
        return true;
    }
    return false;
}
