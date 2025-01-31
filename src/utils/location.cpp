// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "location.h"
#include "error_code.h"
#include <cstdlib>
#include <system_error>
#include <boost/system/error_code.hpp>
#include <boost/nowide/convert.hpp>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <shlobj.h>
#endif

namespace syncspirit::utils {

namespace sys = boost::system;

SYNCSPIRIT_API std::string expand_home(const std::string &path, const home_option_t &home) noexcept {
    if (home.has_value() && path.size() >= 2 && path[0] == '~' && path[1] == '/') {
        auto path_view = std::string_view(path).substr(2);
        return (home.assume_value() / path_view).generic_string();
    }
    return path;
}

outcome::result<bfs::path> get_home_dir() noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    wchar_t appdata[MAX_PATH] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK) {
        return error_code_t::cant_determine_config_dir;
    }
    return bfs::path(appdata);
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    auto *pw = getpwuid(getuid());
    if (!pw) {
        return sys::error_code{errno, sys::generic_category()};
    }
#if defined(__unix__)
    if (auto xdg_home = std::getenv("XDG_CONFIG_HOME")) {
        return bfs::path(xdg_home);
    } else {
        return bfs::path(pw->pw_dir);
    }
#else
    return bfs::path(pw->pw_dir);
#endif

#endif
}

outcome::result<bfs::path> get_default_config_dir() noexcept {
    auto home_opt = get_home_dir();
    if (home_opt.has_error()) {
        return home_opt.assume_error();
    }
    auto home = home_opt.assume_value();

#if defined(__unix__)
    home /= ".config";
#endif
    home /= "syncspirit";
    return home;
}

} // namespace syncspirit::utils
