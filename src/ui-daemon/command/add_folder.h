#pragma once

#include "../command.h"
#include "../../model/folder.h"

namespace syncspirit::daemon::command {

struct add_folder_t final : command_t {
    bool execute(governor_actor_t &) noexcept override;
    static outcome::result<command_ptr_t> construct(const std::string_view &in) noexcept;

    inline add_folder_t() noexcept {};
    inline add_folder_t(db::Folder &&f) noexcept : folder{std::move(f)} {}

  private:
    db::Folder folder;
};

} // namespace syncspirit::daemon::command
