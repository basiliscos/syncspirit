// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_folders.h"
#include "unknown_folder.h"

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

unknown_folders_t::unknown_folders_t(model::device_t &peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), peer{peer_} {
    label("unknown folders");

    for (auto &it : supervisor.get_cluster()->get_unknown_folders()) {
        auto &uf = *it.item;
        if (uf.device_id() == peer.device_id()) {
            uf.set_augmentation(add_unknown_folder(uf));
        }
    }
}

augmentation_ptr_t unknown_folders_t::add_unknown_folder(model::unknown_folder_t &uf) {
    return within_tree([&]() {
        auto node = new unknown_folder_t(uf, supervisor, tree());
        add(prefs(), node->label(), node);
        return node->get_proxy();
    });
}

void unknown_folders_t::remove_folder(tree_item_t *item) {
    remove_child(item);
    tree()->redraw();
}
