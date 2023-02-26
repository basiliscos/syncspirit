// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <rotor/actor_base.h>
#include "model/folder.h"

namespace syncspirit::test {
namespace {
namespace to {
struct device {};
struct state {};
} // namespace to
} // namespace
} // namespace syncspirit::test

namespace syncspirit::model {

template <> inline auto &folder_t::access<test::to::device>() noexcept { return device; }

} // namespace syncspirit::model

namespace rotor {

template <> inline auto &actor_base_t::access<syncspirit::test::to::state>() noexcept { return state; }

} // namespace rotor
