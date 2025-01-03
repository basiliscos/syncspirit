#pragma once

#include "../tree_item.h"
#include "utils/string_comparator.hpp"
#include <set>
#include <map>

namespace syncspirit::fltk::tree_item {

#if 0
struct entry_t;

struct file_visitor_t {
    virtual ~file_visitor_t() = default;
    virtual void visit(const model::file_info_t &file, void *) const = 0;
};

struct entry_t : tree_item_t {
    using parent_t = tree_item_t;
    using items_t = std::set<entry_t *>;
    using dirs_map_t = std::map<std::string, entry_t *, utils::string_comparator_t>;

    entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation);
    ~entry_t();

    virtual model::file_info_t *get_entry() = 0;

    virtual entry_t *locate_own_dir(std::string_view name);
    virtual entry_t *locate_dir(const bfs::path &parent);
    virtual void add_entry(model::file_info_t &file);
    virtual void show_deleted(bool value);
    virtual void apply(const file_visitor_t &visitor, void *data);
    virtual void apply(const node_visitor_t &visitor, void *data);
    virtual void assign(entry_t &);

    void remove_child(tree_item_t *child) override;

    void remove_node(entry_t *child);
    bool insert_node(entry_t *node);

    void make_hierarchy(model::file_infos_map_t &files);

    virtual entry_t *make_entry(model::file_info_t *file, std::string filename) = 0;

    int dirs_count;
    dirs_map_t dirs_map;
    items_t orphaned_items;
    items_t deleted_items;
};
#endif

struct entry_t : dynamic_item_t {
    using parent_t = dynamic_item_t;
    entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, augmentation_entry_t* augmentation, tree_item_t* parent = nullptr);

    void update_label() override;
    void on_open() override;
    void show_deleted(bool value) override;

    void hide();
    void show();

    dynamic_item_t* create(augmentation_entry_t&) override;

    tree_item_t* parent;
    bool expanded;
};

} // namespace syncspirit::fltk::tree_item
