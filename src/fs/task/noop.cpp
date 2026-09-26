// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "noop.h"

using namespace syncspirit::fs::task;

bool noop_t::process(fs_slave_t &, execution_context_t &) noexcept { return false; }
