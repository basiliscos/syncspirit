// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct entry_t : dynamic_item_t {
    using parent_t = dynamic_item_t;
    entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, augmentation_entry_t *augmentation,
            tree_item_t *parent = nullptr);

    void update_label() override;
    void on_update() override;
    void on_open() override;
    void show_deleted(bool value) override;

    void hide();
    void show();

    dynamic_item_t *create(augmentation_entry_t &) override;
    virtual entry_t *create_child(augmentation_entry_t &entry) = 0;
    virtual void refresh_children() override;

    tree_item_t *parent;
    bool expanded;
};

} // namespace syncspirit::fltk::tree_item
