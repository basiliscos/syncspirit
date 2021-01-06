#pragma once

#include "activity.h"
#include "../ui/messages.hpp"

namespace syncspirit::console {

struct new_folder_activity_t : activity_t {
    new_folder_activity_t(tui_actor_t &actor_, ui::message::new_folder_notify_t &message) noexcept;
    bool handle(const char key) noexcept override;
    void display() noexcept override;
    void display_default() noexcept;

    bool operator==(const activity_t &other) const noexcept override;
    proto::Folder folder;
    model::device_ptr_t source;
};

} // namespace syncspirit::console
