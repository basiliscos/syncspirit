// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <algorithm>
#include <cstdint>
#include <cassert>
#include <utility>

namespace syncspirit::utils {

template <typename T, typename S = std::uint32_t> struct compact_vector_t {
    compact_vector_t() = default;
    compact_vector_t(const compact_vector_t &other) { (*this) = other; }
    compact_vector_t(compact_vector_t &&other) { (*this) = std::move(other); }
    ~compact_vector_t() {
        if (ptr) {
            for (auto i = 0; i < sz; ++i) {
                ptr[i].~T();
            }
            delete[] ptr;
        }
    }
    compact_vector_t(const T *from, const T *to) {
        assert(to >= from);
        sz = static_cast<S>(to - from);
        ptr = new T[sz];
        std::copy(from, to, ptr);
    }

    compact_vector_t &operator=(compact_vector_t &&other) {
        std::swap(ptr, other.ptr);
        std::swap(sz, other.sz);
        return *this;
    }

    compact_vector_t &operator=(const compact_vector_t &other) {
        if (ptr) {
            delete[] ptr;
        }
        sz = other.sz;
        ptr = new T[sz];
        for (auto i = 0; i < sz; ++i) {
            ptr[i] = other.ptr[i];
        }
        return *this;
    }

    void resize(S new_size) {
        if (ptr) {
            if (new_size < sz) {
                for (S i = new_size; i < sz; ++i) {
                    ptr[i] = {};
                }
            } else {
                auto new_ptr = new T[new_size]();
                std::move(ptr, ptr + sz, new_ptr);
                delete[] ptr;
                ptr = new_ptr;
            }
        } else if (new_size) {
            ptr = new T[new_size]();
        }
        sz = new_size;
    }

    T *data() { return ptr; }

    const T *data() const { return ptr; }

    T *begin() { return ptr; }

    const T *begin() const { return ptr; }

    T *end() { return ptr + sz; }

    const T *end() const { return ptr + sz; }

    S size() const { return sz; }

    bool empty() const { return sz == 0; }

    bool operator==(const compact_vector_t &other) const noexcept {
        if (sz == other.sz) {
            for (S i = 0; i < sz; ++i) {
                if (ptr[i] != other.ptr[i]) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    const T &front() const {
        assert(sz);
        return ptr[0];
    }

    const T &back() const {
        assert(sz);
        return ptr[sz - 1];
    }

    T &operator[](S index) {
        assert(index < sz);
        return ptr[index];
    }

    const T &operator[](S index) const {
        assert(index < sz);
        return ptr[index];
    }

    T *ptr = nullptr;
    S sz = 0;
};

} // namespace syncspirit::utils
