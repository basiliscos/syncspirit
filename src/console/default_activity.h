#pragma once

#include "activity.h"

namespace syncspirit::console {

struct default_activity_t : activity_t {
    default_activity_t(tui_actor_t &actor_, activity_type_t type_) noexcept;
    bool handle(const char key) noexcept override;
    void display() noexcept override;
    void forget() noexcept override;
    bool operator==(const activity_t &other) const noexcept override;
};

} // namespace syncspirit::console
