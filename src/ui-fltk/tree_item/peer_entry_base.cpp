#include "peer_entry_base.h"
#include "../utils.hpp"

using namespace syncspirit::fltk::tree_item;

peer_entry_base_t::peer_entry_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation)
    : parent_t(supervisor, tree, has_augmentation), dirs_count{0} {}

peer_entry_base_t::~peer_entry_base_t() {
    for (auto item : orphaned_items) {
        delete item;
    }
    orphaned_items.clear();
}

auto peer_entry_base_t::locate_dir(const bfs::path &parent) -> virtual_entry_t * {
    auto current = (virtual_entry_t *)(this);
    for (auto &piece : parent) {
        auto name = piece.string();
        current = current->locate_own_dir(name);
    }
    return current;
}

virtual_entry_t *peer_entry_base_t::locate_own_dir(std::string_view name) {
    if (name.empty()) {
        return this;
    }

    auto it = dirs_map.find(name);
    assert(it != dirs_map.end());
    return static_cast<virtual_entry_t *>(it->second);
}

void peer_entry_base_t::add_entry(model::file_info_t &file) {
    bool deleted = file.is_deleted();
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto start_index = int{0};
    auto end_index = int{0};
    auto t = tree();
    auto node = within_tree([&]() -> peer_entry_base_t * { return make_entry(file); });
    node->update_label();
    auto name = std::string(node->label());

    if (file.is_dir()) {
        dirs_map[name] = node;
    }
    if (deleted) {
        deleted_items.emplace(node);
    }

    if (!deleted || show_deleted) {
        insert_node(node);
    } else {
        orphaned_items.emplace(node);
    }
}

void peer_entry_base_t::remove_child(tree_item_t *child) { remove_node(static_cast<peer_entry_base_t *>(child)); }

void peer_entry_base_t::remove_node(peer_entry_base_t *child) {
    auto &entry = *child->get_entry();
    if (entry.is_dir()) {
        --dirs_count;
    }
    if (child->augmentation) {
        if (entry.is_deleted()) {
            deleted_items.emplace(child);
        } else {
            auto it = deleted_items.find(child);
            if (it != deleted_items.end()) {
                deleted_items.erase(it);
            }
        }
        auto index = find_child(child);
        deparent(index);
        orphaned_items.emplace(child);
        update_label();
        tree()->redraw();
    } else {
        orphaned_items.erase(child);
        deleted_items.erase(child);
        parent_t::remove_child(child);
    }
}

void peer_entry_base_t::insert_node(peer_entry_base_t *node) {
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto start_index = int{0};
    auto end_index = int{0};
    auto directory = node->get_entry()->is_dir();

    if (directory) {
        end_index = dirs_count - 1;
        ++dirs_count;
    } else {
        start_index = dirs_count;
        end_index = children() - 1;
    }

    auto pos = bisect(node->label(), start_index, end_index, children(), name_provider);
    auto tmp_node = insert(prefs(), "", pos);
    replace_child(tmp_node, node);

    if (directory) {
        tree()->close(node);
    }
}

void peer_entry_base_t::show_deleted(bool value) {
    for (auto &it : dirs_map) {
        it.second->show_deleted(value);
    }

    for (auto node : deleted_items) {
        if (!node->get_entry()->is_dir()) {
            node->show_deleted(value);
        }

        if (value) {
            insert_node(node);
            orphaned_items.erase(node);
        } else {
            remove_child(node);
        }
    }
    tree()->redraw();
}

void peer_entry_base_t::apply(const entry_visitor_t &visitor, void *data) {
    auto entry = get_entry();
    if (entry) {
        visitor.visit(*entry, data);
    }
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    for (int i = 0; i < children(); ++i) {
        auto &child_entry = static_cast<peer_entry_base_t &>(*child(i));
        child_entry.apply(visitor, data);
    }

    if (!show_deleted) {
        for (auto &it : orphaned_items) {
            it->apply(visitor, data);
        }
    }
}

void peer_entry_base_t::make_hierarchy(model::file_infos_map_t &files_map) {
    using files_t = std::vector<model::file_info_ptr_t>;
    auto files = files_t();
    files.reserve(files_map.size());
    for (auto &it : files_map) {
        files.push_back(it.item);
    }
    auto sorter = [](const model::file_info_ptr_t &l, const model::file_info_ptr_t &r) {
        return l->get_name() < r->get_name();
    };
    std::sort(files.begin(), files.end(), sorter);

    for (auto &file : files) {
        auto path = bfs::path(file->get_name());
        auto dir = locate_dir(path.parent_path());
        dir->add_entry(*file);
    }
    tree()->redraw();
}
