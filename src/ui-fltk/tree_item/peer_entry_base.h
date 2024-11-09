#pragma once

#include "../tree_item.h"
#include "utils/string_comparator.hpp"
#include <set>
#include <map>

namespace syncspirit::fltk::tree_item {

struct entry_visitor_t {
    virtual ~entry_visitor_t() = default;
    virtual void visit(const model::file_info_t &file, void *) const = 0;
};

struct peer_entry_base_t : tree_item_t {
    using parent_t = tree_item_t;
    using items_t = std::set<peer_entry_base_t *>;
    using dirs_map_t = std::map<std::string, peer_entry_base_t *, utils::string_comparator_t>;

    peer_entry_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation);
    ~peer_entry_base_t();

    virtual model::file_info_t *get_entry() = 0;

    virtual peer_entry_base_t *locate_own_dir(std::string_view name);
    virtual peer_entry_base_t *locate_dir(const bfs::path &parent);
    virtual void add_entry(model::file_info_t &file);
    virtual void show_deleted(bool value);
    virtual void apply(const entry_visitor_t &visitor, void *data);

    void remove_child(tree_item_t *child) override;

    void remove_node(peer_entry_base_t *child);
    void insert_node(peer_entry_base_t *node);

    void make_hierarchy(model::file_infos_map_t &files);

    virtual peer_entry_base_t *make_entry(model::file_info_t &file) = 0;

    int dirs_count;
    dirs_map_t dirs_map;
    items_t orphaned_items;
    items_t deleted_items;
};

} // namespace syncspirit::fltk::tree_item
