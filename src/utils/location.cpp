// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "location.h"
#include <cstdlib>

#if defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#include <windows.h>
#include <shlobj.h>
#include "syncspirit-config.h"
#endif

namespace syncspirit::utils {

poly_path_view_t expand_home(const std::string &path, const poly_path_view_t &home) noexcept {
    if (!home.empty() && path.size() >= 2 && path[0] == '~' && (path[1] == '/' || path[1] == '\\')) {
        auto path_view = std::string_view(path).substr(2);
        auto path_wstr = boost::nowide::widen(path_view);
        return home / make_native_view(path_view, home.get_allocator());
    }
    return make_native_view(path, home.get_allocator());
}

poly_path_view_t get_home_dir(const allocator_t &allocator) noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
    wchar_t appdata[SYNCSPIRIT_PATH_MAX] = {0};
    if (SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, appdata) != S_OK) {
        return {allocator};
    }
    return make_native_view(appdata, allocator);
#elif defined(__unix__) || (defined(__APPLE__) && defined(__MACH__))
    auto *pw = getpwuid(getuid());
    if (!pw) {
        return {allocator};
    }
#if defined(__unix__)
    if (auto xdg_home = std::getenv("XDG_CONFIG_HOME")) {
        return make_native_view(xdg_home, allocator);
    } else {
        return make_native_view(pw->pw_dir, allocator);
    }
#else
    return make_native_view(pw->pw_dir, allocator);
#endif

#endif
}

poly_path_view_t get_default_config_dir(const allocator_t &allocator) noexcept {
    auto home = get_home_dir(allocator);
    if (home.empty()) {
        return home;
    }

#if defined(__unix__)
    return home / make_native_view(".config/syncspirit", allocator);
#endif
    return home / make_native_view("syncspirit", allocator);
}

} // namespace syncspirit::utils
