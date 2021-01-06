#pragma once

#include "activity.h"
#include "../config/main.h"

namespace syncspirit::console {

struct config_activity_t : activity_t {
    static const constexpr size_t MAX_DEVICE_NAME = 30;

    enum class sub_activity_t {
        main,
        editing_device,
    };

    config_activity_t(tui_actor_t &actor_, config::main_t &config_,
                      config::main_t &config_orig_) noexcept;

    bool handle(const char key) noexcept override;
    void display() noexcept override;
    void forget() noexcept override;
    bool locked() noexcept override;

  private:
    void display_menu() noexcept;
    void display_device() noexcept;
    bool handle_main(const char key) noexcept;
    bool handle_device(const char key) noexcept;

    config::main_t &config;
    config::main_t &config_orig;
    sub_activity_t sub_activity;
    char buff[MAX_DEVICE_NAME] = {0};
};

} // namespace syncspirit::console
