#include "entry.h"
#include "../utils.hpp"
#include "peer_entry.h"

using namespace syncspirit::fltk::tree_item;

entry_t::entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, augmentation_entry_t* augmentation_, tree_item_t* parent_):
    parent_t(supervisor, tree, false), parent{parent_}, expanded{false}
{
    if (augmentation_) {
        augmentation = augmentation_;

        if (!augmentation_->get_children().empty()) {
            add(prefs(), "[dummy]", new tree_item_t(supervisor, tree, false));
            tree->close(this, 0);
        }

        update_label();
    }
}

void entry_t::update_label() {
    auto aug = static_cast<augmentation_entry_t*>(augmentation.get());
    auto context = color_context_t::unknown;
    if (auto file = aug->get_file(); file) {
        if (file->is_deleted()) {
            context = color_context_t::deleted;
        } else if (file->is_link()) {
            context = color_context_t::link;
        } else if (file->is_global()) {
            context = color_context_t::actualized;
        }
    }
    auto color = supervisor.get_color(context);
    labelfgcolor(color);
    label(aug->get_own_name().data());
    tree()->redraw();
}

void entry_t::on_open() {
    if (expanded || !children()) {
        return;
    }

    auto dummy = child(0);
    Fl_Tree_Item::remove_child(dummy);
    expanded = true;

    auto entry = static_cast<augmentation_entry_root_t*>(augmentation.get());

    for (auto& it: entry->get_children()) {
        it->display();
    }
}

auto entry_t::create(augmentation_entry_t& entry) -> dynamic_item_t* {
    bool display_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    bool create_node = display_deleted || !entry.get_file()->is_deleted();
    if (create_node) {
        return within_tree([&]() -> dynamic_item_t* {
            auto child_node = new peer_entry_t(supervisor, tree(), &entry, this);
            auto position = entry.get_position(display_deleted);
            auto tmp_node = insert(prefs(), "", position);
            replace_child(tmp_node, child_node);
            tree()->redraw();
            return child_node;
        });
    }
    return {};
}

void entry_t::show() {
    bool display_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    auto index = parent->find_child(this);
    if (index == -1) {
        auto aug = static_cast<augmentation_entry_base_t*>(augmentation.get());
        auto position = aug->get_position(display_deleted);
        parent->reparent(this, position);
    }
}

void entry_t::hide() {
    auto index = parent->find_child(this);
    if (index >= 0) {
        parent->deparent(index);
    }
}

void entry_t::show_deleted(bool value) {
    auto aug = static_cast<augmentation_entry_base_t*>(augmentation.get());
    for (auto& child_aug : aug->get_children()) {
        if (expanded && value) {
            child_aug->display();
        }
        if (auto child_node = child_aug->get_owner(); child_node) {
            auto child_entry = static_cast<entry_t*>(child_node);
            child_entry->show_deleted(value);
        }
    }
    if (auto file = aug->get_file(); file) {
        if (!value && file->is_deleted()) {
            hide();
        } else if (value && file->is_deleted()) {
            show();
        }
    }
    tree()->redraw();
}

#if 0
entry_t::entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation)
    : parent_t(supervisor, tree, has_augmentation), dirs_count{0} {}

entry_t::~entry_t() {
    for (auto item : orphaned_items) {
        delete item;
    }
    orphaned_items.clear();
}

auto entry_t::locate_dir(const bfs::path &parent) -> entry_t * {
    auto current = this;
    for (auto &piece : parent) {
        auto name = piece.string();
        current = current->locate_own_dir(name);
    }
    return current;
}

entry_t *entry_t::locate_own_dir(std::string_view name) {
    if (name.empty()) {
        return this;
    }

    auto it = dirs_map.find(name);
    if (it != dirs_map.end()) {
        return it->second;
    }

    auto label = std::string(name);
    auto node = within_tree([&]() -> entry_t * { return make_entry(nullptr, label); });
    node->update_label();

    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto pos = bisect(node->label(), 0, dirs_count - 1, children(), name_provider);
    auto tmp_node = insert(prefs(), "", pos);
    replace_child(tmp_node, node);
    tree()->close(node);
    dirs_map[label] = node;

    ++dirs_count;
    assert(dirs_count <= children());
    return node;
}

void entry_t::add_entry(model::file_info_t &file) {
    bool deleted = file.is_deleted();
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto t = tree();
    auto node = within_tree([&]() -> entry_t * { return make_entry(&file, file.get_path().filename().string()); });
    node->update_label();
    insert_node(node);
    if (deleted) {
        if (!show_deleted) {
            remove_node(node);
        } else {
            deleted_items.emplace(node);
        }
    }
}

void entry_t::remove_child(tree_item_t *child) { remove_node(static_cast<entry_t *>(child)); }

void entry_t::remove_node(entry_t *child) {
    auto entry = child->get_entry();
    if ((entry && entry->is_dir()) || !entry) {
        --dirs_count;
        assert(dirs_count >= 0);
    }
    if (child->augmentation) {
        auto index = find_child(child);
        auto has_been_shown = index >= 0;
        if (entry && has_been_shown) {
            deleted_items.emplace(child);
        } else {
            auto it = deleted_items.find(child);
            if (it != deleted_items.end()) {
                deleted_items.erase(it);
            }
        }
        if (has_been_shown) {
            deparent(index);
        }
        if (entry->is_deleted()) {
            orphaned_items.emplace(child);
        } else {
            auto it = orphaned_items.find(child);
            if (it != orphaned_items.end()) {
                orphaned_items.erase(it);
            }
        }
        update_label();
        tree()->redraw();
    } else {
        orphaned_items.erase(child);
        deleted_items.erase(child);
        parent_t::remove_child(child);
    }
}

bool entry_t::insert_node(entry_t *node) {
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto start_index = int{0};
    auto end_index = int{0};
    auto directory = node->get_entry()->is_dir();

    if (directory) {
        end_index = dirs_count - 1;
    } else {
        start_index = dirs_count;
        end_index = children() - 1;
    }

    auto pos = bisect(node->label(), start_index, end_index, children(), name_provider);
    bool exists = false;
    if (directory && pos <= end_index) {
        auto previous = reinterpret_cast<entry_t *>(child(pos));
        if (previous->label() == std::string_view(node->label())) {
            assert(previous->dirs_count <= previous->children());
            for (auto i = 0; previous->children(); ++i) {
                auto c = previous->deparent(0);
                node->reparent(c, i);
            }
            assert(!previous->children());
            node->dirs_count = previous->dirs_count;
            node->dirs_map = std::move(previous->dirs_map);
            node->orphaned_items = std::move(previous->orphaned_items);
            node->deleted_items = std::move(previous->deleted_items);
            previous->augmentation.reset();
            remove_node(previous);
        }
    }

    if (!exists) {
        auto tmp_node = insert(prefs(), "", pos);
        replace_child(tmp_node, node);

        if (directory) {
            auto name = std::string(node->label());
            dirs_map[name] = node;
            ++dirs_count;
            tree()->close(node);
        }
    }

    return exists;
}

void entry_t::show_deleted(bool value) {
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

void entry_t::apply(const file_visitor_t &visitor, void *data) {
    auto entry = get_entry();
    if (entry) {
        visitor.visit(*entry, data);
    }
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    for (int i = 0; i < children(); ++i) {
        auto &child_entry = static_cast<entry_t &>(*child(i));
        child_entry.apply(visitor, data);
    }

    if (!show_deleted) {
        for (auto &it : orphaned_items) {
            it->apply(visitor, data);
        }
    }
}

void entry_t::apply(const node_visitor_t &visitor, void *data) {
    visitor.visit(*this, data);
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    for (int i = 0; i < children(); ++i) {
        auto &child_entry = static_cast<entry_t &>(*child(i));
        child_entry.apply(visitor, data);
    }

    if (!show_deleted) {
        for (auto &it : orphaned_items) {
            it->apply(visitor, data);
        }
    }
}

void entry_t::make_hierarchy(model::file_infos_map_t &files_map) {
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

void entry_t::assign(entry_t &) { assert(0 && "should not happen"); }

#endif
