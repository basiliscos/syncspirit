// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-config.h"

#if SYNCSPIRIT_WATCHER_INOTIFY
#include "linux/watcher.h"
#else
#include "watcher_base.h"
#endif

namespace syncspirit::fs::platform {

#if SYNCSPIRIT_WATCHER_INOTIFY
using watcher_t = linux::watcher_t;
#else
using watcher_t = watcher_base_t;
#endif

} // namespace syncspirit::fs::platform
