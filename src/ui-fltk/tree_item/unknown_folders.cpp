// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_folders.h"
#include "unknown_folder.h"

#include "model/diff/peer/cluster_update.h"
#include "model/diff/modify/add_unknown_folders.h"
#include "model/diff/modify/remove_unknown_folders.h"

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

unknown_folders_t::unknown_folders_t(model::device_ptr_t peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), model_sub(supervisor.add(this)), peer{std::move(peer_)} {
    label("unknown folders");

    for (auto &uf : supervisor.get_cluster()->get_unknown_folders()) {
        if (uf->device_id() == peer->device_id()) {
            add_unknown_folder(uf);
        }
    }
}

void unknown_folders_t::add_unknown_folder(model::unknown_folder_ptr_t uf) {
    auto node = new unknown_folder_t(uf, supervisor, tree());
    add(prefs(), node->label(), node);
}

void unknown_folders_t::operator()(model::message::model_update_t &update) {
    std::ignore = update.payload.diff->visit(*this, nullptr);
}

auto unknown_folders_t::operator()(const model::diff::peer::cluster_update_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.model::diff::aggregate_t::visit(*this, custom);
}

auto unknown_folders_t::operator()(const model::diff::modify::add_unknown_folders_t &diff, void *) noexcept
    -> outcome::result<void> {
    tree()->begin();
    int added = 0;
    auto &unknown_folders = supervisor.get_cluster()->get_unknown_folders();
    for (auto &item : diff.container) {
        if (item.peer_id == peer->device_id().get_sha256()) {
            for (auto &uf : unknown_folders) {
                if (uf->device_id() == peer->device_id() && uf->get_id() == item.db.folder().id()) {
                    add_unknown_folder(uf);
                    ++added;
                    break;
                }
            }
        }
    }
    tree()->end();
    if (added) {
        tree()->redraw();
    }
    return outcome::success();
}

auto unknown_folders_t::operator()(const model::diff::modify::remove_unknown_folders_t &diff, void *) noexcept
    -> outcome::result<void> {
    int removed = 0;
    for (auto &key : diff.keys) {
        for (int i = 0; i < children(); ++i) {
            auto node = static_cast<unknown_folder_t *>(child(i));
            if (node->folder->get_key() == key) {
                auto prev = tree()->next_item(this, FL_Up, true);
                tree()->select(prev, 1);
                remove_child(node);
                ++removed;
            }
        }
    }
    if (removed) {
        tree()->redraw();
    }
    return outcome::success();
}
