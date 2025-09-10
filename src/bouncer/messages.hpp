// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <rotor.hpp>

namespace syncspirit::bouncer {

namespace r = rotor;

namespace payload {

using package_t = r::message_ptr_t;

} // namespace payload

namespace message {

using package_t = r::message_t<payload::package_t>;

} // namespace message

} // namespace syncspirit::bouncer
