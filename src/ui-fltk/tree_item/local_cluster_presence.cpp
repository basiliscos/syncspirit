// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "local_cluster_presence.h"
#include "presentation/cluster_file_presence.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = syncspirit::presentation::presence_t::features_t;

local_cluster_presence_t::local_cluster_presence_t(presentation::presence_t &presence_, app_supervisor_t &supervisor,
                                                   Fl_Tree *tree)
    : parent_t(presence_, supervisor, tree) {
    auto f = presence_.get_features();
    assert(f & (F::local | F::cluster));
    update_label();
    if (f & F::directory) {
        populate_dummy_child();
    }
}

void local_cluster_presence_t::update_label() {
    auto &p = static_cast<presentation::cluster_file_presence_t &>(presence);
    auto name = p.get_entity()->get_path().get_own_name();
    auto color = get_color();
    labelfgcolor(color);
    label(name.data());
}
