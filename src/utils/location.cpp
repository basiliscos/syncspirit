// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "location.h"
#include "error_code.h"
#include <cstdlib>
#include <system_error>
#include <boost/system/error_code.hpp>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

namespace syncspirit::utils {

namespace sys = boost::system;

outcome::result<fs::path> get_home_dir() noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    if (auto local_app = std::getenv("LocalAppData")) {
        return fs::path(local_app);
    } else if (auto app_data = std::getenv("AppData")) {
        return fs::path(app_data);
    }
    return error_code_t::cant_determine_config_dir;
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    auto *pw = getpwuid(getuid());
    if (!pw) {
        return sys::error_code{errno, sys::generic_category()};
    }
#if defined(__unix__)
    if (auto xdg_home = std::getenv("XDG_CONFIG_HOME")) {
        return fs::path(xdg_home);
    } else {
        return fs::path(pw->pw_dir);
    }
#else
    return fs::path(pw->pw_dir);
#endif

#endif
}

outcome::result<fs::path> get_default_config_dir() noexcept {
    auto home = get_home_dir();
    if (home.has_error()) {
        return home.assume_error();
    }
    return home.assume_value().append("syncspirit");
}

} // namespace syncspirit::utils
