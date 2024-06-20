// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_folders.h"
#include "unknown_folder.h"

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

unknown_folders_t::unknown_folders_t(model::device_ptr_t peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), model_sub(supervisor.add(this)), peer{std::move(peer_)} {
    label("unknown folders");

    for (auto &uf : supervisor.get_cluster()->get_unknown_folders()) {
        if (uf->device_id() == peer->device_id()) {
            auto node = new unknown_folder_t(uf, supervisor, tree);
            add(prefs(), node->label(), node);
        }
    }
}
