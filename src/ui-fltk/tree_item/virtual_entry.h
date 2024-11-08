#pragma once

#include "model/file_info.h"
#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct entry_visitor_t {
    virtual ~entry_visitor_t() = default;
    virtual void visit(const model::file_info_t &file, void *) const = 0;
};

struct virtual_entry_t : tree_item_t {
    using tree_item_t::tree_item_t;
    virtual model::file_info_t *get_entry() = 0;
    virtual virtual_entry_t *locate_dir(const bfs::path &parent) = 0;
    virtual virtual_entry_t *locate_own_dir(std::string_view name) = 0;
    virtual void add_entry(model::file_info_t &file) = 0;
    virtual void show_deleted(bool value) = 0;
    virtual void apply(const entry_visitor_t &visitor, void *data) = 0;
};

} // namespace syncspirit::fltk::tree_item
