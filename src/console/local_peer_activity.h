#pragma once

#include "activity.h"
#include "../ui/messages.hpp"

namespace syncspirit::console {

struct local_peer_activity_t : activity_t {
    using message_ptr_t = ui::payload::discovery_notification_t::message_ptr_t;
    static const constexpr size_t MAX_DEVICE_NAME = 30;

    enum class sub_activity_t {
        main,
        editing_name,
    };

    local_peer_activity_t(tui_actor_t &actor_, activity_type_t type_,
                          ui::message::discovery_notify_t &message) noexcept;
    bool handle(const char key) noexcept override;
    bool handle_main(const char key) noexcept;
    bool handle_label(const char key) noexcept;

    void display() noexcept override;
    void display_menu() noexcept;
    void display_label() noexcept;

    bool operator==(const activity_t &other) const noexcept override;

    std::string address;
    std::string_view short_id;
    message_ptr_t message;
    sub_activity_t sub_activity;
    char buff[MAX_DEVICE_NAME] = {0};
};

} // namespace syncspirit::console
