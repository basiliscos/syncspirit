// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "peer_folders.h"
#include "folder.h"
#include "presentation/folder_entity.h"
#include "presentation/folder_presence.h"

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
        if (auto fi = f.is_shared_with(peer); fi) {
            auto &augmentation = f.get_augmentation();
            if (augmentation) {
                auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation.get());
                auto presence = folder_entity->get_presence(peer);
                auto folder_presence = static_cast<presentation::folder_presence_t *>(presence);
                auto folder = new folder_t(*folder_presence, supervisor, tree);
                insert_by_label(folder);
            }
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

void peer_folders_t::add_folder(presentation::folder_presence_t &presence) {
    return within_tree([&]() {
        auto folder = new folder_t(presence, supervisor, tree());
        insert_by_label(folder);
        update_label();
        return;
    });
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
