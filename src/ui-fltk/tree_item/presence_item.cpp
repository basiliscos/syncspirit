// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "presence_item.h"
#include "local_cluster_presence.h"
#include "missing_item_presence.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = syncspirit::presentation::presence_t::features_t;

presence_item_t::presence_item_t(presentation::presence_t &presence_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, false), presence{presence_}, expanded{false} {
    presence.set_augmentation(*this);
}

presence_item_t::~presence_item_t() {
    auto p = parent();
    if (p) {
        do_hide();
    }
}

void presence_item_t::on_open() {
    if (expanded || !children()) {
        return;
    }

    auto dummy = child(0);
    Fl_Tree_Item::remove_child(dummy);
    expanded = true;

    int position = 0;
    auto hide_mask = supervisor.mask_nodes();
    for (auto &child : presence.get_entity()->get_children()) {
        using F = presentation::presence_t::features_t;

        auto p = child->get_presence(*presence.get_device());
        auto f = p->get_features();
        auto node = (presence_item_t *)(nullptr);
        if (f & (F::cluster | F::local)) {
            if ((f & F::directory) || (f & F::file)) {
                node = new local_cluster_presence_t(*p, supervisor, tree());
            }
        } else if (f & F::missing) {
            node = new missing_item_presence_t(*p, supervisor, tree());
        }
        if (node) {
            auto tmp_node = insert(prefs(), "", position++);
            replace_child(tmp_node, node);
            node->show(hide_mask, false);
        }
    }
    tree()->redraw();
}

void presence_item_t::populate_dummy_child() {
    if (presence.get_entity()->get_children().size() > 0) {
        auto t = tree();
        add(prefs(), "[dummy]", new tree_item_t(supervisor, t, false));
        t->close(this, 0);
    }
}

auto presence_item_t::get_presence() -> presentation::presence_t & { return presence; }

int presence_item_t::get_position(std::uint32_t cut_mask) {
    auto &container = presence.get_parent()->get_entity()->get_children();
    int position = 0;
    for (auto &it : container) {
        auto p = it->get_presence(*presence.get_device());
        if (p == &presence) {
            break;
        }
        if (!(p->get_features() & cut_mask)) {
            ++position;
        }
    }
    return position;
}

void presence_item_t::do_show(std::uint32_t mask) {
    auto host = parent();
    if (!host) {
        auto parent = presence.get_parent();
        if (parent) {
            host = static_cast<presence_item_t *>(parent->get_augmentation().get());
            if (host) {
                auto index = host->find_child(this);
                if (index == -1) {
                    auto position = get_position(mask);
                    host->reparent(this, position);
                }
            }
        }
    }
}

void presence_item_t::do_hide() {
    auto host = parent();
    auto index = host->find_child(this);
    if (index >= 0) {
        host->deparent(index);
    }
}

void presence_item_t::show(std::uint32_t hide_mask, bool recurse) {
    auto hide = presence.get_features() & hide_mask;
    if (!hide) {
        do_show(hide_mask);
    } else {
        do_hide();
    }
    if (recurse && expanded) {
        auto &children = presence.get_entity()->get_children();
        for (auto &child : children) {
            auto p = child->get_presence(*presence.get_device());
            auto item = dynamic_cast<presence_item_t *>(p->get_augmentation().get());
            item->show(hide_mask, recurse);
        }
    }
    tree()->redraw();
}

Fl_Color presence_item_t::get_color() const {
    auto &cfg = supervisor.get_app_config().fltk_config;
    if (cfg.display_colorized) {
        auto f = presence.get_features();
        if (f & F::missing) {
            return FL_DARK_RED;
        } else if (f & F::deleted) {
            return FL_DARK1;
        } else if (f & F::symblink) {
            return FL_DARK_BLUE;
        } else if (f & F::in_sync) {
            return FL_DARK_GREEN;
        } else if (f & F::conflict) {
            return FL_RED;
        }
    }
    return FL_BLACK;
}

void presence_item_t::refresh_children() {}
