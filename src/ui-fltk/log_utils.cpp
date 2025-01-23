// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "log_utils.h"

namespace syncspirit::fltk {

const char *eol =
#ifdef _WIN32
    "\r\n"
#else
    "\n"
#endif
    ;

} // namespace syncspirit::fltk
