#include "entry.h"
#include "../utils.hpp"

using namespace syncspirit::fltk::tree_item;

entry_t::entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, augmentation_entry_t *augmentation_, tree_item_t *parent_)
    : parent_t(supervisor, tree, false), parent{parent_}, expanded{false} {
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
    auto aug = static_cast<augmentation_entry_t *>(augmentation.get());
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

    auto entry = static_cast<augmentation_entry_root_t *>(augmentation.get());

    for (auto &it : entry->get_children()) {
        it->display();
    }
}

void entry_t::on_update() {
    parent_t::on_update();
    refresh_children();
}

auto entry_t::create(augmentation_entry_t &entry) -> dynamic_item_t * {
    bool display_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    bool create_node = display_deleted || !entry.get_file()->is_deleted();
    if (create_node) {
        return within_tree([&]() -> dynamic_item_t * {
            auto child_node = create_child(entry);
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
        auto aug = static_cast<augmentation_entry_base_t *>(augmentation.get());
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
    auto aug = static_cast<augmentation_entry_base_t *>(augmentation.get());
    for (auto &child_aug : aug->get_children()) {
        if (expanded && value) {
            child_aug->display();
        }
        if (auto child_node = child_aug->get_owner(); child_node) {
            auto child_entry = static_cast<entry_t *>(child_node);
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

void entry_t::refresh_children() {
    auto entry = static_cast<augmentation_entry_base_t *>(augmentation.get());
    if (expanded) {
        for (auto &child_aug : entry->get_children()) {
            child_aug->display();
        }
    } else if (children() == 0 && entry->get_children().size()) {
        within_tree([&]() -> void {
            auto t = tree();
            add(prefs(), "[dummy]", new tree_item_t(supervisor, t, false));
            t->close(this, 0);
        });
    }
}
