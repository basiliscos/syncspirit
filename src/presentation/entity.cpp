// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include <cassert>
#include <utility>

using namespace syncspirit;
using namespace syncspirit::presentation;

using F = presence_t::features_t;

entity_t::entity_t(path_t path_, entity_t *parent_) noexcept
    : parent{parent_}, path(std::move(path_)), best{nullptr}, entities_monitor{nullptr} {}

entity_t::~entity_t() { clear_children(); }

void entity_t::clear_children() noexcept {
    while (children.size()) {
        auto child = *children.begin();
        remove_child(*child);
    }
}

auto entity_t::get_path() const noexcept -> const path_t & { return path; }

auto entity_t::get_children() noexcept -> children_t & { return children; }

entity_t *entity_t::get_parent() noexcept { return parent; }

void entity_t::set_parent(entity_t *value) noexcept {
    parent = value;
    for (auto presence : records) {
        presence->set_parent(value);
    }
}

void entity_t::push_stats(const presence_stats_t &diff, const model::device_t *source, bool best) noexcept {
    auto current = this;
    while (current) {
        if (best) {
            assert(current->statistics.size >= 0);
            assert(current->statistics.entities >= 0);
            current->statistics += diff;
            assert(current->statistics.size >= 0);
            assert(current->statistics.entities >= 0);
            ++current->generation;
        }
        if (source) {
            for (auto p : current->records) {
                if (p->device == source) {
                    p->entity_generation--;
                    assert(p->statistics.size >= 0);
                    assert(p->statistics.entities >= 0);
                    assert(p->statistics.cluster_entries >= 0 || p->entity_generation != current->generation);
                    p->statistics += diff;
                    assert(p->statistics.size >= 0);
                    assert(p->statistics.entities >= 0);
                    assert(p->statistics.cluster_entries >= 0 || p->entity_generation != current->generation);
                    break;
                }
            }
        }
        current = current->parent;
    }
}

void entity_t::remove_presense(presence_t &item) noexcept {
    auto predicate = [&item](const presence_t *presence) { return presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    if (children.size()) {
        bool rescan_children = true;
        while (rescan_children) {
            bool scan = true;
            for (auto it = children.begin(); scan && it != children.end();) {
                auto &c = *it;
                bool advance_it = true;
                for (auto p : c->records) {
                    if (p->device == item.device) {
                        p->clear_presense();
                        advance_it = false;
                        scan = false;
                        break;
                    }
                }
                if (advance_it) {
                    ++it;
                }
            }
            rescan_children = !scan;
        }
    }
    bool need_restat = false;
    auto stats = statistics;
    auto own_stats = item.get_own_stats();
    if (&item == best) {
        stats -= own_stats;
        need_restat = true;
        ++own_stats.cluster_entries;
    }
    push_stats(-own_stats, item.device, false);
    records.erase(it);
    if (need_restat) {
        if (auto best = recalc_best(); best) {
            stats += best->get_own_stats();
        }
    }
    if (stats != statistics) {
        auto diff = stats - statistics;
        push_stats({diff, 0}, {}, true);
    };

    bool remove_self = records.empty() && parent;
    if (records.size() == 1) {
        auto p = records.front();
        if (!p->device) {
            remove_presense(*p);
        }
    }
    if (remove_self) {
        clear_children();
        parent->remove_child(*this);
        parent = nullptr;
    }
}

auto entity_t::recalc_best() noexcept -> const presence_t * {
    auto best = (const presence_t *)(nullptr);
    if (parent) {
        best = {};
        for (auto p : records) {
            if (!p->device) {
                continue;
            }
            if (!best) {
                best = p;
            } else {
                best = p->determine_best(best);
            }
        }
    }
    this->best = best;
    return best;
}

presence_t *entity_t::get_presence(model::device_t &device) noexcept {
    presence_t *fallback = nullptr;
    for (auto p : records) {
        if (p->device == &device) {
            return p;
        } else if (!p->device) {
            fallback = p;
        }
    }
    return fallback;
}

void entity_t::add_child(entity_t &child) noexcept {
    model::intrusive_ptr_add_ref(&child);
    children.emplace(&child);
    child.set_parent(this);
}

void entity_t::remove_child(entity_t &child) noexcept {
    auto it = children.equal_range(&child).first;
    assert(it != children.end());
    child.clear_children();
    child.set_augmentation({});
    model::intrusive_ptr_release(&child);

    for (auto r : records) {
        r->children.clear();
    }
    child.parent = nullptr;
    push_stats({-child.get_stats(), 0}, nullptr, true);

    if (auto monitor = get_monitor(); monitor) {
        monitor->on_delete(child);
    }

    children.erase(it);
}

const entity_stats_t &entity_t::get_stats() noexcept { return statistics; }

void entity_t::commit(const path_t &path, const model::device_t *device) noexcept {
    bool do_recurse = (device == nullptr);
    if (!do_recurse) {
        for (size_t i = 0; (i < records.size()) && !do_recurse; ++i) {
            auto p = records[i];
            if (p->device == device) {
                do_recurse = true;
            };
        }
    }
    if (!do_recurse) {
        return;
    }

    for (auto child : children) {
        child->commit(path, device);
        statistics += child->get_stats();
    }
    for (auto p : records) {
        auto parent = p->parent;
        if (parent && path.contains(parent->entity->path)) {
            parent->statistics += p->statistics;
        }
    }

    auto best = recalc_best();
    if (best) {
        statistics += best->get_own_stats();
    }
    ++generation;
    if (auto monitor = get_monitor(); monitor) {
        monitor->on_update(*this);
    }
}

auto entity_t::monitor(entities_monitor_t *monitor_) noexcept -> monitor_guard_t {
    this->entities_monitor = monitor_;
    return monitor_guard_t(this);
}

using nc_t = entity_t::name_comparator_t;

bool nc_t::operator()(const entity_t *lhs, const entity_t *rhs) const noexcept {
    return lhs->get_path().get_own_name() < rhs->get_path().get_own_name();
}

bool nc_t::operator()(const entity_t *lhs, const std::string_view rhs) const noexcept {
    return lhs->get_path().get_own_name() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const entity_t *rhs) const noexcept {
    return lhs < rhs->get_path().get_own_name();
}

using mg_t = entity_t::monitor_guard_t;

mg_t::monitor_guard_t(entity_t *entity_) noexcept : entity{entity_} {}

mg_t::~monitor_guard_t() { entity->entities_monitor = {}; }
