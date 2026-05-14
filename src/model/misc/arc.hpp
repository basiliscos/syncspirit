// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <cstdint>

namespace syncspirit::model {

template <typename T> struct arc_base_t : boost::intrusive_ref_counter<T, boost::thread_unsafe_counter> {
    using parent_t = boost::intrusive_ref_counter<T, boost::thread_unsafe_counter>;
    using parent_t::parent_t;
    arc_base_t(const arc_base_t &) = delete;
    arc_base_t(arc_base_t &&) = default;
};

template <typename T> using intrusive_ptr_t = boost::intrusive_ptr<T>;

template <typename T> struct rc_guard_t {
    rc_guard_t(T *ptr_ = nullptr) noexcept : ptr{ptr_} {
        if (ptr) {
            inc();
        }
    }

    ~rc_guard_t() {
        auto copy = sub_counter;
        for (std::uint32_t i = 0; i < copy; ++i) {
            dec();
        }
    }

    void inc() {
        ++sub_counter;
        intrusive_ptr_add_ref(ptr);
    }

    std::uint32_t dec() {
        --sub_counter;
        intrusive_ptr_release(ptr);
        return sub_counter;
    }

    T *ptr;
    std::uint32_t sub_counter{0};
};

} // namespace syncspirit::model
