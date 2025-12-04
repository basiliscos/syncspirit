// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "presence_item.h"
#include "presence_item/cluster.h"
#include "presence_item/missing.h"
#include "presentation/folder_presence.h"
#include "symbols.h"

#include <fmt/format.h>
#include <memory_resource>
#include <iterator>
#include <cassert>

using namespace syncspirit::presentation;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = presence_t::features_t;

inline static bool is_hidden(presence_t *presence, std::uint32_t hide_mask) {
    auto features = presence->get_features();
    auto hide = features & hide_mask;

    // don't show missing & deleted
    if (!hide && features & F::missing) {
        auto best = presence->get_entity()->get_best();
        if (best) {
            hide = best->get_features() & hide_mask;
        }
    }
    return hide;
}

static auto make_item(presence_item_t *parent, presence_t &presence) -> presence_item_ptr_t {
    if (auto &aug = presence.get_augmentation(); aug) {
        return static_cast<presence_item_t *>(aug.get());
    }
    auto f = presence.get_features();
    if (f & F::cluster) {
        if ((f & F::directory) || (f & F::file)) {
            return new cluster_t(presence, parent->supervisor, parent->tree());
        }
    } else if (f & F::missing) {
        return new missing_t(parent, presence);
    }
    return {};
}

static auto get_host(presence_item_t *self) -> presence_item_t * {
    auto presence = &self->get_presence();
    auto parent = presence->get_parent();
    if (parent) {
        return static_cast<presence_item_t *>(parent->get_augmentation().get());
    } else if (presence->get_features() & F::missing) {
        return static_cast<missing_t *>(self)->host;
    }
    return nullptr;
}

presence_item_t::presence_item_t(presence_t &presence_, app_supervisor_t &supervisor, Fl_Tree *tree, bool augment)
    : parent_t(supervisor, tree, false), presence{&presence_}, expanded{false} {
    if (augment) {
        assert(!presence->get_augmentation());
        presence->set_augmentation(*this);
    }
}

presence_item_t::~presence_item_t() {
    auto p = parent();
    if (p) {
        auto self = do_hide();
        self.detach();
    }

    // will be de-allocated by augmentation, not by fltk
    auto child_count = children();
    while (children() > 0) {
        safe_detach(0);
    }
    if (presence) {
        presence->get_augmentation().detach();
    }
}

void presence_item_t::on_open() {
    if (expanded || !children()) {
        return;
    }

    safe_detach(0);
    expanded = true;

    int position = 0;
    auto hide_mask = supervisor.mask_nodes();
    for (auto c : presence->get_children()) {
        if (!is_hidden(c, hide_mask)) {
            auto node = make_item(this, *c);
            if (node) {
                insert_node(node, position++);
                node->show(hide_mask, false, false);
            }
        }
    }
    tree()->redraw();
}

void presence_item_t::populate_dummy_child() {
    if (presence->get_entity()->get_children().size() > 0) {
        auto t = tree();
        add(prefs(), "[dummy]", new tree_item_t(supervisor, t, false));
        t->close(this, 0);
    }
}

auto presence_item_t::get_presence() -> presentation::presence_t & { return *presence; }

auto presence_item_t::get_device() const -> const model::device_t * { return presence->get_device(); }

int presence_item_t::get_position(const presence_item_t &child, std::uint32_t cut_mask) {
    auto &container = presence->get_children();
    int position = 0;
    for (auto p : container) {
        if (p == child.presence) {
            break;
        }
        if (!is_hidden(p, cut_mask)) {
            ++position;
        }
    }
    return position;
}

presence_item_ptr_t presence_item_t::do_hide() {
    auto host = parent();
    auto r = presence_item_ptr_t(this);
    if (host) {
        auto index = host->find_child(this);
        if (index >= 0) {
            if (is_selected()) {
                select_other();
            }
            if ((presence->get_features() & F::folder)) {
                host->deparent(index);
            } else {
                dynamic_cast<presence_item_t *>(host)->safe_detach(index);
            }
            host->tree()->redraw();
        }
    }
    return r;
}

bool presence_item_t::show(std::uint32_t hide_mask, bool refresh_labels, int32_t depth) {
    using nodes_storage_t = std::unordered_map<presentation::entity_t *, presence_item_ptr_t>;
    assert(depth >= 0);
    if (is_hidden(presence, hide_mask)) {
        do_hide();
        return false;
    }
    if (refresh_labels) {
        update_label();
    }
    auto nodes = nodes_storage_t();
    if (depth >= 1 && expanded) {
        while (children()) {
            auto node = safe_detach(0);
            nodes.emplace(node->get_presence().get_entity(), std::move(node));
        }
        auto get_or_create_child = [&](presence_t *presence) -> presence_item_ptr_t {
            auto it = nodes.find(presence->get_entity());
            if (it != nodes.end() && it->second->presence == presence) {
                auto node = std::move(it->second);
                nodes.erase(it);
                return node;
            }
            return make_item(this, *presence);
        };
        auto &children = presence->get_children();
        for (int i = 0, j = 0; i < (int)children.size(); ++i) {
            auto c = children[i];
            if (!is_hidden(c, hide_mask)) {
                auto item = get_or_create_child(c).detach();
                reparent(item, j++);
                item->show(hide_mask, refresh_labels, depth - 1);
            }
        }
    }
    tree()->redraw();
    return true;
}

void presence_item_t::show_child(presentation::presence_t &child_presence, std::uint32_t mask) {
    auto aug = child_presence.get_augmentation().get();
    if (is_expanded()) {
        bool found = false;
        for (int i = 0; i < this->children(); ++i) {
            assert(dynamic_cast<presence_item_t *>(child(i)));
            auto c = static_cast<presence_item_t *>(child(i));
            auto &p = c->get_presence();
            if (p.get_entity() == child_presence.get_entity()) {
                if (&p != &child_presence) {
                    auto child_holder = presence_item_ptr_t();
                    bool need_expand = c->is_expanded();
                    bool need_open = c->is_open();
                    if (p.get_features() & F::missing) {
                        child_holder = safe_detach(i);
                    }
                    auto node = make_item(this, child_presence);
                    for (int j = 0; child_holder && child_holder->children(); ++j) {
                        auto n = child_holder->deparent(0);
                        node->reparent(n, j);
                    }
                    insert_node(node, i);
                    node->show(mask, true, 0);
                    if (need_expand || need_open) {
                        node->open();
                        node->on_open();
                    }
                    if (!need_open) {
                        node->close();
                    }
                }
                found = true;
                break;
            }
        }
        if (!found) {
            show(mask, true, 1);
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
    auto name = presence->get_entity()->get_path()->get_own_name();
    auto features = presence->get_features();
    auto node_label = string_t(allocator);
    if (features & F::folder) {
        auto folder_presence = static_cast<presentation::folder_presence_t *>(presence);
        auto folder = folder_presence->get_folder_info().get_folder();
        auto folder_label = folder->get_label();
        auto folder_id = folder->get_id();
        fmt::format_to(std::back_inserter(node_label), "{}, {}", folder_label, folder_id);
    } else {
        node_label = string_t(name, allocator);
    }

    if (features & (F::directory | F::folder)) {
        auto &ps = presence->get_stats(true);
        auto &es = presence->get_entity()->get_stats();
        auto has_size = ps.size || (features & F::folder);
        if (es.entities && has_size && (ps.cluster_entries != es.entities)) {
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
        auto f = presence->get_features();
        if (f & F::missing) {
            return FL_DARK_RED;
        } else if (f & F::deleted) {
            return FL_DARK1;
        } else if (f & F::symblink) {
            return FL_DARK_BLUE;
        } else if (f & F::conflict) {
            return FL_RED;
        }
        if (f & F::folder) {
            auto fp = static_cast<presentation::folder_presence_t *>(presence);
            if (fp->get_folder_info().get_folder()->is_suspended()) {
                return FL_DARK_RED;
            }
        }
        auto &ps = presence->get_stats(true);
        auto &s = presence->get_entity()->get_stats();
        auto in_sync = ps.cluster_entries == s.entities;
        if (in_sync) {
            return FL_DARK_GREEN;
        }
    }
    return FL_BLACK;
}

presence_item_ptr_t presence_item_t::safe_detach(int child_index) {
    auto item = presence_item_ptr_t();
    auto child = deparent(child_index);
    if (child) {
        auto presence_item = dynamic_cast<presence_item_t *>(child);
        if (!presence_item) {
            delete child;
        }
        item = presence_item_ptr_t(presence_item, false);
    }
    return item;
}

void presence_item_t::insert_node(presence_item_ptr_t node, int position) {
    auto tmp_node = insert(prefs(), "", position);
    auto ptr = node.detach();
    auto p = ptr->presence;
    if (p->get_features() & F::directory && ptr->children() == 0 && !ptr->is_expanded()) {
        if (p->get_children().size() > 0) {
            ptr->populate_dummy_child();
        }
    }
    replace_child(tmp_node, ptr);
}

bool presence_item_t::is_expanded() const { return expanded; }

void presence_item_t::on_delete() noexcept { presence = {}; }
