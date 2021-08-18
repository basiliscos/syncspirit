#include "command.h"
#include "error_code.h"
#include "command/add_peer.h"
#include "command/add_folder.h"
#include "command/share_folder.h"

namespace syncspirit::daemon {

command_t::~command_t() {}

outcome::result<command_ptr_t> command_t::parse(const std::string_view &in) noexcept {
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
    }
    return make_error_code(error_code_t::unknown_command);
}

} // namespace syncspirit::daemon
