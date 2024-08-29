// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "command.h"
#include "error_code.h"
#include "command/add_peer.h"
#include "command/add_folder.h"
#include "command/inactivate.h"
#include "command/share_folder.h"

namespace syncspirit::daemon {

outcome::result<command_ptr_t> command_t::parse(std::string_view in) noexcept {
    auto colon = in.find(":");
    if (colon == in.npos) {
        return make_error_code(error_code_t::command_is_missing);
    }
    auto cmd = in.substr(0, colon);
    if (cmd == "add_peer") {
        return command::add_peer_t::construct(in.substr(colon + 1));
    } else if (cmd == "add_folder") {
        return command::add_folder_t::construct(in.substr(colon + 1));
    } else if (cmd == "share") {
        return command::share_folder_t::construct(in.substr(colon + 1));
    } else if (cmd == "inactivate") {
        return command::inactivate_t::construct(in.substr(colon + 1));
    }
    return make_error_code(error_code_t::unknown_command);
}

} // namespace syncspirit::daemon
