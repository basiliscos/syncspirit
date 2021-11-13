#pragma once
#include <string>
#include <string_view>
#include <array>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace syncspirit::model {

template<size_t I, typename T>
std::string_view get_index(const T& item) noexcept;

namespace details {

namespace mi = boost::multi_index;

template<size_t I, typename T>
std::string_view get_key(const T& key) noexcept { return get_index<I>(key.item); }

template<size_t N>
using StringArray = std::array<std::string, N>;

template <typename Item, size_t N> struct key_t {
    using Array = StringArray<N>;
    using item_t = Item;
    Item item;
    mutable Array keys;

    key_t(const item_t& item_, const Array& keys_) noexcept:
        item{item_},
        keys{keys_} {
    }

    template<size_t I> std::string get() noexcept {
        return keys.at(I);
    }
};

struct hash_op_t {
    std::size_t operator()(std::string_view v) const noexcept { return std::hash<std::string_view>()(v); }
    std::size_t operator()(const std::string &s) const noexcept { return std::hash<std::string>()(s); }
};

struct eq_op_t {
    std::size_t operator()(const std::string &s1, const std::string &s2) const noexcept { return s1 == s2; }
    std::size_t operator()(const std::string &s1, std::string_view s2) const noexcept { return s1 == s2; }
    std::size_t operator()(std::string_view s1, const std::string &s2) const { return s1 == s2; }
    std::size_t operator()(std::string_view s1, std::string_view s2) const { return s1 == s2; }
};



template <size_t I, typename T>
using hash_fn_t = mi::hashed_unique<mi::global_fun<const T&, std::string_view, &get_key<I, T>>, hash_op_t, eq_op_t>;
//using hash_fn_t = mi::hashed_unique<mi::mem_fun<T, std::string, &T::template get<I>>, hash_op_t, eq_op_t>;


template<size_t N, size_t I, typename T> struct hash_impl_t {
    using type = hash_fn_t<I, key_t<T, N>>;
};

template<size_t I, size_t N, typename T, typename... Ts> struct hasher_t {
    using type = typename hasher_t<I-1, N, T, typename hash_impl_t<N, I-1, T>::type, Ts...>::type;
};

template<size_t N, typename T, typename... Ts> struct hasher_t<0, N, T, Ts...> {
    using type = mi::indexed_by<Ts...>;
};

template <typename T, size_t N = 1>
using unordered_string_map_t = mi::multi_index_container<key_t<T, N>, typename hasher_t<N, N, T>::type>;

}

template <typename Item, size_t N> struct generic_map_t {
    using map_t = details::unordered_string_map_t<Item, N>;
    using array_t = details::StringArray<N>;
    using wrapped_item_t = details::key_t<Item, N>;
    using iterator_t = decltype (std::declval<map_t>().template get<0>().begin());
    using const_iterator_t = decltype (std::declval<map_t>().template get<0>().cbegin());

    void put(const Item &item) noexcept {
        array_t arr;
        fill<N-1>(arr, item);
        key2item.emplace(item, std::move(arr));
    }

    void remove(const Item &item) noexcept {
        auto it = key2item.find(id(item));
        key2item.erase(it);
    }

    template<size_t I = 0>
    Item get(std::string_view id) const noexcept {
        auto& projection = key2item.template get<I>();
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

    void clear() noexcept { key2item.clear(); }

  private:
    template<size_t I>
    static void constexpr fill(array_t& arr, const Item& item) noexcept {
        arr[I] = std::string(get_index<I>(item));
        if constexpr (I > 0) {
            fill<I-1>(arr, item);
        }
    }

    map_t key2item;
};




}; // namespace syncspirit::model
