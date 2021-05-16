#pragma once

#include "activity.h"
#include "../ui/messages.hpp"

namespace syncspirit::console {

struct new_folder_activity_t : activity_t {
    static const constexpr size_t MAX_PATH_SZ = 1024;
    enum class sub_activity_t {
        main,
        editing_path,
    };

    new_folder_activity_t(tui_actor_t &actor_, ui::message::new_folder_notify_t &message) noexcept;
    bool handle(const char key) noexcept override;
    bool handle_main(const char key) noexcept;
    bool handle_path(const char key) noexcept;
    void display() noexcept override;
    void display_default() noexcept;
    void display_path() noexcept;

    bool operator==(const activity_t &other) const noexcept override;
    proto::Folder folder;
    model::device_ptr_t source;
    std::uint64_t source_index;
    sub_activity_t sub_activity;
    char buff[MAX_PATH_SZ] = {0};
};

} // namespace syncspirit::console
