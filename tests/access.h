// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <rotor/actor_base.h>
#include "model/folder.h"
#include "model/file_info.h"

namespace syncspirit::test {
namespace {
namespace to {
struct device {};
struct state {};
struct ignore_delete {};
struct ignore_permissions {};
struct pull_order {};
struct folder_type {};
struct flags {};
} // namespace to
} // namespace
} // namespace syncspirit::test

namespace syncspirit::model {

template <> inline auto &file_info_t::access<test::to::flags>() noexcept { return flags; }
template <> inline auto &folder_t::access<test::to::device>() noexcept { return device; }
template <> inline auto &folder_data_t::access<test::to::ignore_delete>() noexcept { return ignore_delete; }
template <> inline auto &folder_data_t::access<test::to::ignore_permissions>() noexcept { return ignore_permissions; }
template <> inline auto &folder_data_t::access<test::to::pull_order>() noexcept { return pull_order; }
template <> inline auto &folder_data_t::access<test::to::folder_type>() noexcept { return folder_type; }

} // namespace syncspirit::model

namespace rotor {

template <> inline auto &actor_base_t::access<syncspirit::test::to::state>() noexcept { return state; }

} // namespace rotor
