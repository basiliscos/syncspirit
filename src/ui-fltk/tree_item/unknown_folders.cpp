// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_folders.h"
#include "unknown_folder.h"

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

unknown_folders_t::unknown_folders_t(model::device_t &peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, false), peer{peer_} {
    label("unknown folders");

    auto &folders = supervisor.get_cluster()->get_unknown_folders();
    for (auto &it : folders) {
        auto &uf = *it.item;
        if (uf.device_id() == peer.device_id() && !uf.get_augmentation()) {
            uf.set_augmentation(add_unknown_folder(uf));
        }
    }
}

augmentation_ptr_t unknown_folders_t::add_unknown_folder(model::unknown_folder_t &uf) {
    for (int i = 0; i < children(); ++i) {
        auto item = static_cast<unknown_folder_t *>(this->child(i));
        if (&item->folder == &uf) {
            return {};
        }
    }
    return within_tree([&]() { return insert_by_label(new unknown_folder_t(uf, supervisor, tree()))->get_proxy(); });
}

void unknown_folders_t::remove_folder(tree_item_t *item) {
    remove_child(item);
    tree()->redraw();
}
