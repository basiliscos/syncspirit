#pragma once
#include <unordered_map>
#include <string>
#include <cstdint>
#include <type_traits>

namespace syncspirit::model {

template <typename Item, typename Id, typename Key = std::uint64_t> struct generic_map_t {
    using id2d_t = std::unordered_map<Id, Item>;
    using key2d_t = std::unordered_map<Key, Item>;
    using iterator = typename key2d_t::iterator;
    using const_iterator = typename key2d_t::const_iterator;

    void put(const Item &item) noexcept {
        id2d.emplace(natural_key(item), item);

        auto key = db_key(item);
        if constexpr (std::is_integral_v<Key>) {
            if (key) {
                key2d.emplace(key, item);
            }
        } else {
            key2d.emplace(key, item);
        }
    }

    void remove(const Item &item) noexcept {
        auto it_id = id2d.find(natural_key(item));
        id2d.erase(it_id);

        auto it_k = key2d.find(db_key(item));
        key2d.erase(it_k);
    }

    Item by_id(const Id &id) const noexcept {
        auto it = id2d.find(id);
        if (it != id2d.end()) {
            return it->second;
        }
        return {};
    }
    Item by_key(const Key &id) const noexcept {
        auto it = key2d.find(id);
        if (it != key2d.end()) {
            return it->second;
        }
        return {};
    }

    size_t size() const noexcept { return id2d.size(); }

    iterator begin() noexcept { return key2d.begin(); }

    const_iterator begin() const noexcept { return key2d.cbegin(); }

    iterator end() noexcept { return key2d.end(); }

    const_iterator end() const noexcept { return key2d.cend(); }

    void clear() noexcept {
        id2d.clear();
        key2d.clear();
    }

    /*  private: */
    id2d_t id2d;
    key2d_t key2d;
};

template <typename Item, typename Key> struct generic_map_t<Item, void, Key> {
    using key2d_t = std::unordered_map<Key, Item>;
    using iterator = typename key2d_t::iterator;

    void put(const Item &item) noexcept { key2d.emplace(db_key(item), item); }

    void remove(const Item &item) noexcept {
        auto it_k = key2d.find(db_key(item));
        key2d.erase(it_k);
    }

    Item by_key(const Key &id) const noexcept {
        auto it = key2d.find(id);
        if (it != key2d.end()) {
            return it->second;
        }
        return {};
    }

    size_t size() const noexcept { return key2d.size(); }

    iterator begin() noexcept { return key2d.begin(); }

    iterator end() noexcept { return key2d.end(); }

    void clear() noexcept { key2d.clear(); }

  private:
    key2d_t key2d;
};

}; // namespace syncspirit::model
