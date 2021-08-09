#pragma once

#include "../command.h"
#include "../../model/device_id.h"

namespace syncspirit::daemon::command {

struct add_peer_t final : command_t {
    static outcome::result<command_ptr_t> construct(const std::string_view &in) noexcept;
    bool execute(governor_actor_t &) noexcept override;

    inline add_peer_t() noexcept {};
    inline add_peer_t(model::device_id_t peer_, const std::string_view label_) noexcept : peer{peer_}, label(label_) {}

  private:
    model::device_id_t peer;
    std::string label;
};

} // namespace syncspirit::daemon::command
