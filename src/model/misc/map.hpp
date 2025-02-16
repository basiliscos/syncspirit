// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <tuple>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace syncspirit::model {

namespace details {

namespace mi = boost::multi_index;

template <size_t N> struct string_tuple_t;
template <> struct string_tuple_t<1> {
    using key_storage_t = std::tuple<std::string>;
};
template <> struct string_tuple_t<2> {
    using key_storage_t = std::tuple<std::string, std::string>;
};
template <> struct string_tuple_t<3> {
    using key_storage_t = std::tuple<std::string, std::string, std::string>;
};

template <typename Item, size_t N> struct indexed_item_t {
    using storage_t = typename string_tuple_t<N>::key_storage_t;
    using item_t = Item;
    static constexpr size_t size = N;
    item_t item;
    mutable storage_t keys;

    indexed_item_t(const item_t &item_, const storage_t &keys_) noexcept : item{item_}, keys{keys_} {}

    template <size_t I> auto &get() noexcept { return std::get<I>(keys); }
};

template <typename T> struct reduced_type_t {
    using type = T;
};

template <> struct reduced_type_t<std::string> {
    using type = std::string_view;
};

template <typename Key, size_t I>
using single_key_t = reduced_type_t<std::tuple_element_t<I, typename Key::storage_t>>::type;

} // namespace details

// template <size_t I, typename T> std::string_view get_index(const T &item) noexcept;
template <size_t I, typename Result, typename T> Result get_index(const T &item) noexcept;

namespace details {

template <size_t I, typename K> single_key_t<K, I> get_key(const K &key) noexcept {
    return get_index<I, single_key_t<K, I>>(key.item);
}

struct hash_op_t {
    std::size_t operator()(std::string_view v) const noexcept { return std::hash<std::string_view>()(v); }
    std::size_t operator()(const std::string &s) const noexcept { return std::hash<std::string>()(s); }
    std::size_t operator()(std::int64_t v) const noexcept { return std::hash<std::int64_t>()(v); }
};

struct eq_op_t {
    std::size_t operator()(const std::string &s1, const std::string &s2) const noexcept { return s1 == s2; }
    std::size_t operator()(const std::string &s1, std::string_view s2) const noexcept { return s1 == s2; }
    std::size_t operator()(std::string_view s1, const std::string &s2) const { return s1 == s2; }
    std::size_t operator()(std::string_view s1, std::string_view s2) const { return s1 == s2; }
    std::size_t operator()(std::int64_t v1, std::int64_t v2) const { return v1 == v2; }
};

template <size_t I, typename K> struct indexed_by {
    using type = mi::hashed_unique<mi::global_fun<const K &, single_key_t<K, I>, &get_key<I, K>>, hash_op_t, eq_op_t>;
};
// using hash_fn_t = mi::hashed_unique<mi::mem_fun<T, std::string, &T::template get<I>>, hash_op_t, eq_op_t>;

template <size_t N, size_t I, typename Key> struct hash_impl_t {
    using type = indexed_by<I, Key>::type;
};

template <size_t I, size_t N, typename K, typename... Ks> struct hasher_t {
    using type = typename hasher_t<I - 1, N, K, typename hash_impl_t<N, I - 1, K>::type, Ks...>::type;
};

template <size_t N, typename K, typename... Ks> struct hasher_t<0, N, K, Ks...> {
    using type = mi::indexed_by<Ks...>;
};

template <typename T, size_t N = 1>
using unordered_string_map_t =
    mi::multi_index_container<indexed_item_t<T, N>, typename hasher_t<N, N, indexed_item_t<T, N>>::type>;

template <typename Item, typename WrappedItem> struct generalized_map_t {
    static constexpr size_t N = WrappedItem::size;
    using map_t = details::unordered_string_map_t<Item, N>;
    using composite_key_t = typename WrappedItem::storage_t;
    using iterator_t = decltype(std::declval<map_t>().template get<0>().begin());
    using const_iterator_t = decltype(std::declval<map_t>().template get<0>().cbegin());

    void put(const Item &item) noexcept {
        composite_key_t arr;
        fill<N - 1>(arr, item);
        auto r = key2item.emplace(item, std::move(arr));
        if (!r.second) {
            remove(item);
            fill<N - 1>(arr, item);
            r = key2item.emplace(item, std::move(arr));
            assert(r.second);
        }
    }

    void remove(const Item &item) noexcept {
        remove<0>(item);
        // key2item.template get<0>().erase(get_index<0>(item));
    }

    template <size_t I = 0> Item get(details::single_key_t<WrappedItem, I> id) const noexcept {
        auto &projection = key2item.template get<I>();
        auto it = projection.find(id);
        if (it != projection.end()) {
            return (*it).item;
        }
        return {};
    }

    size_t size() const noexcept { return key2item.size(); }

    iterator_t begin() noexcept { return key2item.template get<0>().begin(); }

    iterator_t end() noexcept { return key2item.template get<0>().end(); }

    const_iterator_t begin() const noexcept { return key2item.template get<0>().cbegin(); }

    const_iterator_t end() const noexcept { return key2item.template get<0>().cend(); }

    bool operator==(const generalized_map_t &other) const noexcept {
        auto &keys = key2item.template get<0>();
        auto &other_keys = other.key2item.template get<0>();
        if (keys.size() == other.size()) {
            for (auto &it : *this) {
                auto &key = std::get<0>(it.keys);
                auto other_it = other_keys.find(key);
                if (other_it == other_keys.end()) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    void clear() noexcept { key2item.clear(); }

  protected:
    template <size_t I> static void constexpr fill(composite_key_t &arr, const Item &item) noexcept {
        using single_key_t = details::single_key_t<WrappedItem, I>;
        std::get<I>(arr) = get_index<I, single_key_t>(item);
        if constexpr (I > 0) {
            fill<I - 1>(arr, item);
        }
    }

    template <size_t I> void remove(const Item &item) noexcept {
        using single_key_t = details::single_key_t<WrappedItem, I>;
        key2item.template get<I>().erase(get_index<I, single_key_t>(item));
        if constexpr (I + 1 < N) {
            remove<I + 1>(item);
        }
    }

    map_t key2item;
};

} // namespace details

template <typename Item, size_t N>
using generic_map_t = details::generalized_map_t<Item, details::indexed_item_t<Item, N>>;

}; // namespace syncspirit::model
