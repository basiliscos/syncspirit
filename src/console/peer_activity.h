#pragma once

#include "activity.h"
#include "../ui/messages.hpp"
#include <optional>

namespace syncspirit::console {

struct peer_activity_t : activity_t {
    static const constexpr size_t MAX_DEVICE_NAME = 30;

    enum class sub_activity_t {
        main,
        editing_name,
    };

    peer_activity_t(tui_actor_t &actor_, ui::message::discovery_notify_t &message) noexcept;
    peer_activity_t(tui_actor_t &actor_, ui::message::auth_notify_t &message) noexcept;
    bool handle(const char key) noexcept override;
    bool handle_main(const char key) noexcept;
    bool handle_label(const char key) noexcept;

    void display() noexcept override;
    void display_menu() noexcept;
    void display_label() noexcept;

    bool operator==(const activity_t &other) const noexcept override;

    std::optional<std::string> cert_name;
    std::string peer_details;
    std::string peer_name;
    model::device_id_t device_id;
    sub_activity_t sub_activity;
    char buff[MAX_DEVICE_NAME] = {0};
};

} // namespace syncspirit::console
