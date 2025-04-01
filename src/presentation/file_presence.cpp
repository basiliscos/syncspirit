// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file_presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

auto file_presence_t::get_presence_feautres() -> std::uint32_t { return features; }
