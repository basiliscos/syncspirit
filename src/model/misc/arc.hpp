// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::model {

template <typename T> struct arc_base_t : boost::intrusive_ref_counter<T, boost::thread_unsafe_counter> {
    using parent_t = boost::intrusive_ref_counter<T, boost::thread_unsafe_counter>;
    using parent_t::parent_t;
    arc_base_t(const arc_base_t &) = delete;
    arc_base_t(arc_base_t &&) = default;
};

template <typename T> using intrusive_ptr_t = boost::intrusive_ptr<T>;

} // namespace syncspirit::model
