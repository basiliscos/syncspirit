// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "pending_folders.h"
#include "pending_folder.h"

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

pending_folders_t::pending_folders_t(model::device_t &peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, false), peer{peer_} {
    label("pending folders");

    auto &folders = supervisor.get_cluster()->get_pending_folders();
    for (auto &it : folders) {
        auto &f = *it.item;
        if (f.device_id() == peer.device_id() && !f.get_augmentation()) {
            f.set_augmentation(add_pending_folder(f));
        }
    }
}

augmentation_ptr_t pending_folders_t::add_pending_folder(model::pending_folder_t &uf) {
    for (int i = 0; i < children(); ++i) {
        auto item = static_cast<pending_folder_t *>(this->child(i));
        if (&item->folder == &uf) {
            return {};
        }
    }
    return within_tree([&]() { return insert_by_label(new pending_folder_t(uf, supervisor, tree()))->get_proxy(); });
}

void pending_folders_t::remove_folder(tree_item_t *item) {
    remove_child(item);
    tree()->redraw();
}
