#include "default_activity.h"
#include "tui_actor.h"
#include "sink.h"
#include <fmt/fmt.h>

using namespace syncspirit::console;

default_activity_t::default_activity_t(tui_actor_t &actor_, activity_type_t type_) noexcept
    : activity_t{actor_, type_} {}

void default_activity_t::display() noexcept { display_default(); }

void default_activity_t::display_default() noexcept {
    auto c = actor.tui_config.key_help;
    auto p = fmt::format("{}{}A{}, {}{}{}{} - help > ", sink_t::yellow, actor.activities_count - 1, sink_t::reset,
                         sink_t::bold, sink_t::white, std::string_view(&c, 1), sink_t::reset);
    actor.set_prompt(std::string(p.begin(), p.end()));
    details_shown = false;
}

bool default_activity_t::handle(const char pressed) noexcept {
    auto &c = actor.tui_config;
    if (pressed == 27) {
        if (details_shown) {
            display_default();
            return true;
        }
        return false;
    }

    if (pressed != c.key_help) {
        return false;
    }
    auto letter = [](char c) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, std::string_view(&c, 1), sink_t::reset);
    };
    auto key = [](const char *val) -> std::string {
        return fmt::format("{}{}{}{}", sink_t::bold, sink_t::white, val, sink_t::reset);
    };

    auto p = fmt::format("[{}] - quit,  [{}] - more logs, [{}] - less logs, [{}] - config, [{}] - back > ",
                         letter(c.key_quit), letter(c.key_more_logs), letter(c.key_less_logs), letter(c.key_config),
                         key("ESC"));
    actor.set_prompt(std::string(p.begin(), p.end()));
    details_shown = true;
    return true;
}
