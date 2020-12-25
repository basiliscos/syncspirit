#pragma once

#include "activity.h"

namespace syncspirit::console {

struct default_activity_t : activity_t {
    default_activity_t(tui_actor_t &actor_, activity_type_t type_) noexcept;
    bool handle(const char key) noexcept override;
    void display() noexcept override;
    void display_default() noexcept;

    bool details_shown = false;
};

} // namespace syncspirit::console
