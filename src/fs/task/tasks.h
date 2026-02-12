// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "noop.h"
#include "remove_file.h"
#include "rename_file.h"
#include "scan_dir.h"
#include "segment_iterator.h"
#include <variant>

namespace syncspirit::fs {

using task_t =
    std::variant<task::scan_dir_t, task::segment_iterator_t, task::remove_file_t, task::rename_file_t, task::noop_t>;
using tasks_t = std::list<task_t>;

} // namespace syncspirit::fs
