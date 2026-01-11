// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include "linux/context.h"
#elif SYNCSPIRIT_WATCHER_WIN32
#include "windows/context.h"
#else
#include "generic/context.h"
#endif

namespace syncspirit::fs::platform {

#if SYNCSPIRIT_WATCHER_INOTIFY
using context_t = linux::platform_context_t;
#elif SYNCSPIRIT_WATCHER_WIN32
using context_t = windows::platform_context_t;
#else
using context_t = generic::platform_context_t;
#endif

} // namespace syncspirit::fs::platform
