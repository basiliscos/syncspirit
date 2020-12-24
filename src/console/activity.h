#pragma once

namespace syncspirit {
namespace console {

struct tui_actor_t;

enum class activity_type_t {
    DEFAULT,
    LOCAL_PEER,
};

struct activity_t {
    activity_t(tui_actor_t &actor_, activity_type_t type_) noexcept : actor{actor_}, type{type_} {}
    virtual void display() noexcept = 0;
    virtual bool handle(const char key) noexcept = 0;
    virtual void forget() noexcept;
    virtual bool operator==(const activity_t &other) const noexcept = 0;

    tui_actor_t &actor;
    activity_type_t type;
};

} // namespace console
} // namespace syncspirit