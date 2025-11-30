// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "noop.h"

using namespace syncspirit::fs::task;

void noop_t::process(fs_slave_t &, hasher::hasher_plugin_t *) noexcept {}
