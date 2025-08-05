// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "cluster.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = syncspirit::presentation::presence_t::features_t;

cluster_t::cluster_t(presentation::presence_t &presence_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(presence_, supervisor, tree) {
    auto f = presence_.get_features();
    assert(f & (F::local | F::cluster));
    update_label();
}
