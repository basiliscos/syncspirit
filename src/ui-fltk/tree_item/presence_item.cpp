// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "presence_item.h"
#include "local_cluster_presence.h"
#include "missing_item_presence.h"
#include "presentation/folder_presence.h"
#include "../symbols.h"

#include <fmt/format.h>
#include <memory_resource>
#include <iterator>

using namespace syncspirit::presentation;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = presence_t::features_t;

presence_item_t::presence_item_t(presence_t &presence_, app_supervisor_t &supervisor, Fl_Tree *tree, bool augment)
    : parent_t(supervisor, tree, false), presence{presence_}, expanded{false} {
    if (augment) {
        assert(!presence.get_augmentation());
        presence.set_augmentation(*this);
    }
}

presence_item_t::~presence_item_t() {
    auto p = parent();
    if (p) {
        do_hide();
    }

    // will be de-allocated by augmentation, not by fltk
    auto child_count = children();
    int i = 0;
    while (i < children()) {
        auto c = child(i);
        auto pi = dynamic_cast<presence_item_t *>(c);
        if (pi) {
            deparent(i);
            if (pi->presence.get_features() & F::missing) {
                delete c;
            }
        } else {
            ++i;
        }
    }
}

static auto make_item(presence_item_t *parent, presence_t &presence) -> presence_item_t * {
    if (auto &aug = presence.get_augmentation(); aug) {
        return static_cast<presence_item_t *>(aug.get());
    }
    spdlog::warn("zzz making item for : {}", presence.get_entity()->get_path().get_full_name());
    auto f = presence.get_features();
    if (f & (F::cluster | F::local)) {
        if ((f & F::directory) || (f & F::file)) {
            return new local_cluster_presence_t(presence, parent->supervisor, parent->tree());
        }
    } else if (f & F::missing) {
        return new missing_item_presence_t(parent, presence);
    }
    return nullptr;
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
    for (auto p : presence.get_children()) {
        auto node = make_item(this, *p);
        if (node) {
            auto tmp_node = insert(prefs(), "", position++);
            replace_child(tmp_node, node);
            node->show(hide_mask, false, false);
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

auto presence_item_t::get_device() const -> const model::device_t * { return presence.get_device(); }

int presence_item_t::get_position(const presence_item_t &child, std::uint32_t cut_mask) {
    auto &container = presence.get_children();
    auto child_presence = &child.presence;
    int position = 0;
    for (auto p : container) {
        if (p == child_presence) {
            break;
        }
        if (!(p->get_features() & cut_mask)) {
            ++position;
        }
    }
    return position;
}

void presence_item_t::do_show(std::uint32_t mask, bool refresh_label) {
    auto host = parent();
    if (!host) {
        auto host = [&]() -> presence_item_t * {
            auto parent = presence.get_parent();
            if (parent) {
                return static_cast<presence_item_t *>(parent->get_augmentation().get());
            } else if (presence.get_features() & F::missing) {
                return static_cast<missing_item_presence_t *>(this)->host;
            }
            return nullptr;
        }();
        if (host) {
            auto index = host->find_child(this);
            if (index == -1) {
                auto position = host->get_position(*this, mask);
                host->reparent(this, position);
                update_label();
                refresh_label = false;
            }
        }
    }
    if (refresh_label) {
        update_label();
    }
}

void presence_item_t::do_hide() {
    auto host = parent();
    if (host) {
        auto index = host->find_child(this);
        if (index >= 0) {
            if (is_selected()) {
                select_other();
            }
            host->deparent(index);
            tree()->redraw();
        }
    }
}

bool presence_item_t::show(std::uint32_t hide_mask, bool refresh_labels, int32_t depth) {
    assert(depth >= 0);
    auto features = presence.get_features();
    auto hide = features & hide_mask;

    // don't show missing & deleted
    if (!hide && features & F::missing) {
        auto best = presence.get_entity()->get_best();
        if (best) {
            hide = best->get_features() & hide_mask;
        }
    }
    if (!hide) {
        do_show(hide_mask, refresh_labels);
    } else {
        do_hide();
    }
    if (!hide && depth >= 1 && expanded) {
        auto &children = presence.get_children();
        auto c = (presence_item_t *)(nullptr);
        for (int i = 0, j = 0; i < (int)children.size(); ++i) {
            auto p = children[i];
            auto item = (presence_item_t *)(nullptr);
            if (j < this->children() && !c) {
                c = dynamic_cast<presence_item_t *>(child(j));
            }
            if (c) {
                if (&c->presence == p) {
                    item = c;
                } else {
                    auto r = presence_t::compare(p, &c->presence);
                    if (r) {
                        auto node = deparent(j);
                        if (c->presence.get_features() & F::missing) {
                            delete c;
                        }
                        c = {};
                    }
                }
            }
            if (!item) {
                item = make_item(this, *p);
                ++j;
            }
            auto shown = item->show(hide_mask, refresh_labels, depth - 1);
            if (item == c) {
                c = {};
                if (shown) {
                    ++j;
                }
            }
        }
    }
    tree()->redraw();
    return !hide;
}

void presence_item_t::show_child(presentation::presence_t &child_presence, std::uint32_t mask) {
    auto aug = child_presence.get_augmentation().get();
    if (is_expanded()) {
        for (int i = 0; i < this->children(); ++i) {
            auto c = static_cast<presence_item_t *>(child(i));
            auto &p = c->get_presence();
            if (p.get_entity() == child_presence.get_entity()) {
                if (&p != &child_presence) {
                    bool need_open = c->is_expanded();
                    if (p.get_features() & F::missing) {
                        delete c;
                    }
                    auto node = make_item(this, child_presence);
                    auto tmp_node = insert(prefs(), "", i);
                    replace_child(tmp_node, node);
                    node->show(mask, true, 0);
                    if (need_open) {
                        node->on_open();
                        node->open();
                    }
                }
                break;
            }
        }
    } else if (children() == 0) {
        populate_dummy_child();
    }
}

void presence_item_t::update_label() {
    using allocator_t = std::pmr::polymorphic_allocator<char>;
    using string_t = std::basic_string<char, std::char_traits<char>, allocator_t>;
    auto buffer = std::array<std::byte, 256>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = allocator_t(&pool);

    auto color = get_color();
    auto name = presence.get_entity()->get_path().get_own_name();
    auto features = presence.get_features();
    auto node_label = string_t(allocator);
    if (features & F::folder) {
        auto &folder_presence = static_cast<presentation::folder_presence_t &>(presence);
        auto folder = folder_presence.get_folder_info().get_folder();
        auto folder_label = folder->get_label();
        auto folder_id = folder->get_id();
        fmt::format_to(std::back_inserter(node_label), "{}, {}", folder_label, folder_id);
    } else {
        node_label = string_t(name, allocator);
    }

    if (features & (F::directory | F::folder)) {
        auto &ps = presence.get_stats(true);
        auto &es = presence.get_entity()->get_stats();
        if (ps.size && (ps.cluster_entries != es.entities)) {
            double share = (100.0 * ps.cluster_entries) / es.entities;
            fmt::format_to(std::back_inserter(node_label), " ({}{:.2f}%)", symbols::synchronizing, share);
        }
        if (features & F::local && (ps.local_entries != ps.entities)) {
            double share = (100.0 * ps.local_entries) / es.entities;
            fmt::format_to(std::back_inserter(node_label), " ({}{:.2f}%)", symbols::scanning, share);
        }
    }

    labelfgcolor(color);
    label(node_label.data());
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
        } else if (f & F::conflict) {
            return FL_RED;
        }
        auto &ps = presence.get_stats(true);
        auto &s = presence.get_entity()->get_stats();
        auto in_sync = ps.cluster_entries == s.entities;
        if (in_sync) {
            return FL_DARK_GREEN;
        }
    }
    return FL_BLACK;
}

bool presence_item_t::is_expanded() const { return expanded; }

void presence_item_t::refresh_children() {}
