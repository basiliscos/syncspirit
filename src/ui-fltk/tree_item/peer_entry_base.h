#pragma once

#include "../tree_item.h"
#include "virtual_entry.h"
#include "utils/string_comparator.hpp"
#include <set>
#include <map>

namespace syncspirit::fltk::tree_item {

struct peer_entry_base_t : virtual_entry_t {
    using parent_t = virtual_entry_t;
    using items_t = std::set<peer_entry_base_t *>;
    using dirs_map_t = std::map<std::string, peer_entry_base_t *, utils::string_comparator_t>;

    peer_entry_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation);
    ~peer_entry_base_t();

    virtual_entry_t *locate_own_dir(std::string_view name) override;
    virtual_entry_t *locate_dir(const bfs::path &parent) override;
    void add_entry(model::file_info_t &file) override;
    void show_deleted(bool value) override;
    void remove_child(tree_item_t *child) override;
    void apply(const entry_visitor_t &visitor, void *data) override;

    void remove_node(peer_entry_base_t *child);
    void insert_node(peer_entry_base_t *node);

    virtual peer_entry_base_t *make_entry(model::file_info_t &file) = 0;

    int dirs_count;
    dirs_map_t dirs_map;
    items_t orphaned_items;
    items_t deleted_items;
};

} // namespace syncspirit::fltk::tree_item
