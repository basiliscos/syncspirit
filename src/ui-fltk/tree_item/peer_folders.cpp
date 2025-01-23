// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "peer_folders.h"
#include "peer_folder.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

peer_folders_t::peer_folders_t(model::device_t &peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, false), peer{peer_} {
    update_label();
    tree->close(this, 0);
    auto cluster = supervisor.get_cluster();
    auto &folders = cluster->get_folders();
    for (auto it : folders) {
        auto &f = *it.item;
        if (auto folder_info = f.is_shared_with(peer)) {
            auto folder = new peer_folder_t(*folder_info, supervisor, tree);
            insert_by_label(folder);
        }
    }
}

void peer_folders_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto &folders = cluster->get_folders();
    int count = 0;
    for (auto it : folders) {
        if (it.item->is_shared_with(peer)) {
            ++count;
        }
    }
    auto l = fmt::format("folders ({})", count);
    label(l.data());
    tree()->redraw();
}

augmentation_ptr_t peer_folders_t::add_folder(model::folder_info_t &folder_info) {
    auto augmentation = within_tree([&]() -> augmentation_ptr_t {
        auto item = new peer_folder_t(folder_info, supervisor, tree());
        return insert_by_label(item)->get_proxy();
    });
    update_label();
    return augmentation;
}

void peer_folders_t::remove_child(tree_item_t *item) {
    parent_t::remove_child(item);
    if (!children()) {
        static_cast<tree_item_t *>(parent())->remove_child(this);
    } else {
        update_label();
        tree()->redraw();
    }
}
