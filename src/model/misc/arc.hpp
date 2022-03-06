// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::model {

template <typename T> using arc_base_t = boost::intrusive_ref_counter<T, boost::thread_unsafe_counter>;

template <typename T> using intrusive_ptr_t = boost::intrusive_ptr<T>;

} // namespace syncspirit::model
