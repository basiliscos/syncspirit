#pragma once
#include <string>
#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

namespace syncspirit::model {

namespace details {

namespace mi = boost::multi_index;

template <typename T> std::string get_lru_key(const T &key);

// https://www.boost.org/doc/libs/1_45_0/libs/multi_index/example/serialization.cpp
template <typename Item> class mru_list_t {

    struct tag_seq {};
    struct tag_hash {};

    using item_list_t = mi::multi_index_container<
        Item, mi::indexed_by<
                  mi::sequenced<mi::tag<tag_seq>>,
                  mi::hashed_unique<mi::tag<tag_hash>, mi::global_fun<const Item &, std::string, &get_lru_key<Item>>>>>;

  public:
    using item_t = Item;

    mru_list_t(size_t max_items_) : max_items(max_items_) {}

    void put(const Item &item) noexcept {
        auto p = il.push_front(item);

        if (!p.second) {                      /* duplicate item */
            il.relocate(il.begin(), p.first); /* put in front */
        } else if (il.size() > max_items) {   /* keep the length <= max_num_items */
            il.pop_back();
        }
    }

    void remove(const Item &item) noexcept {
        auto &projection = il.template get<1>();
        auto key = get_lru_key(item);
        auto it = projection.find(key);
        if (it != projection.end()) {
            auto it_0 = il.template project<tag_seq>(it);
            il.erase(it_0);
        }
    }

    item_t get(const std::string &key) noexcept {
        auto &projection = il.template get<1>();
        auto it = projection.find(key);
        if (it != projection.end()) {
            auto it_0 = il.template project<tag_seq>(it);
            il.relocate(il.begin(), it_0);
            return *it;
        }
        return {};
    }

    void clear() noexcept { il.clear(); }

  private:
    item_list_t il;
    std::size_t max_items;
};

} // namespace details

template <typename T> using mru_list_t = details::mru_list_t<T>;

} // namespace syncspirit::model
