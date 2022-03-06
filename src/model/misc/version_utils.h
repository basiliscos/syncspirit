// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include "bep.pb.h"

namespace syncspirit::model {

enum class version_relation_t { identity, older, newer, conflict };

version_relation_t compare(const proto::Vector &lhs, const proto::Vector &rhs) noexcept;

} // namespace syncspirit::model
