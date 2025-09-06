// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include "folder_entity.h"
#include <cassert>
#include <utility>

using namespace syncspirit;
using namespace syncspirit::presentation;

using F = presence_t::features_t;

entity_t::entity_t(model::path_ptr_t path_, entity_t *parent_) noexcept
    : parent{parent_}, path(std::move(path_)), best{nullptr}, entities_monitor{nullptr} {}

entity_t::~entity_t() {
    clear_children();
    while (!presences.empty()) {
        presences.front()->clear_presense();
    }
}

void entity_t::clear_children() noexcept {
    for (auto it = children.begin(); it != children.end();) {
        detach_child(**it);
        it = children.erase(it);
    }
}

void entity_t::set_parent(entity_t *value) noexcept {
    parent = value;
    for (auto presence : presences) {
        presence->set_parent(value);
    }
}

void entity_t::push_stats(const presence_stats_t &diff, const model::device_t *source, bool best) noexcept {
    auto e = this;
    auto p = (presence_t *)(nullptr);
    while (e) {
        if (best) {
            assert(e->statistics.size >= 0);
            assert(e->statistics.entities >= 0);
            e->statistics += diff;
            assert(e->statistics.size >= 0);
            assert(e->statistics.entities >= 0);
            ++e->generation;
        }
        if (source) {
            if (!p) {
                for (auto pp : e->presences) {
                    if (pp->device == source) {
                        p = pp;
                        break;
                    }
                }
            }
            if (!p) { // detached presence
                return;
            }
            p->entity_generation--;
            assert(p->statistics.size >= 0);
            assert(p->statistics.entities >= 0);
            assert(p->statistics.cluster_entries >= 0 || p->entity_generation != e->generation);
            p->statistics += diff;
            assert(p->statistics.size >= 0);
            assert(p->statistics.entities >= 0);
            assert(p->statistics.cluster_entries >= 0 || p->entity_generation != e->generation);
            p = p->parent;
        }
        e = e->parent;
    }
}

void entity_t::remove_presense(presence_t &item) noexcept {
    auto it = std::find(presences.begin(), presences.end(), &item);
    assert(it != presences.end());
    for (auto it = children.begin(); it != children.end();) {
        auto c = *it;
        bool found = false;
        for (auto p : c->presences) {
            if (p->device == item.device) {
                found = true;
                // critical to stop "child-from-parent" removal propagation
                it = children.erase(it);
                p->clear_presense();
                if (c->presences.empty()) {
                    detach_child(*c);
                } else {
                    children.emplace_hint(it, c);
                }
                break;
            }
        }
        if (!found) {
            ++it;
        }
    }

    bool need_restat = &item == best;
    auto own_stats = item.get_own_stats();
    if (need_restat) {
        need_restat = true;
        ++own_stats.cluster_entries;
    }
    push_stats(-own_stats, item.device, false);
    presences.erase(it);
    if (need_restat) {
        auto stats = -own_stats;
        if (auto best = recalc_best(); best) {
            stats += best->get_own_stats();
        }
        push_stats(stats, {}, true);
    }

    if ((presences.size() == 1) && (presences.front()->get_features() & F::missing)) {
        remove_presense(*presences.front());
        if (parent) {
            auto &siblings = parent->children;
            auto it = siblings.find(this);
            if (it != siblings.end()) {
                parent->detach_child(*this);
                siblings.erase(it);
            }
        }
    }
}

auto entity_t::recalc_best() noexcept -> const presence_t * {
    auto best = (const presence_t *)(nullptr);
    if (parent) {
        best = {};
        for (auto p : presences) {
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

presence_t *entity_t::get_presence(const model::device_t *device) noexcept {
    presence_t *r = nullptr;
    for (auto p : presences) {
        if (p->device == device) {
            return p;
        } else if (!p->device) {
            r = p;
        }
    }
    return r;
}

void entity_t::add_child(entity_t &child) noexcept {
    model::intrusive_ptr_add_ref(&child);
    children.emplace(&child);
    child.set_parent(this);
}

void entity_t::detach_child(entity_t &child) noexcept {
    child.clear_children();
    child.set_augmentation({});

    for (auto r : presences) {
        r->clear_children();
    }
    auto &child_presences = child.presences;
    while (!child_presences.empty()) {
        child_presences.front()->clear_presense();
    }

    if (auto monitor = get_monitor(); monitor) {
        monitor->on_delete(child);
    }
    // child.parent = nullptr;
    model::intrusive_ptr_release(&child);
}

void entity_t::commit(const model::path_t &path, const model::device_t *device) noexcept {
    bool do_recurse = (device == nullptr);
    if (!do_recurse) {
        for (size_t i = 0; (i < presences.size()) && !do_recurse; ++i) {
            auto p = presences[i];
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
    }

    auto diff = best ? -best->get_own_stats() : presence_stats_t();

    auto new_best = recalc_best();
    if (new_best) {
        diff += new_best->get_own_stats();
        push_stats(diff, {}, true);
    }
    // TODO: move this up?
    ++generation;
    if (auto monitor = get_monitor(); monitor) {
        monitor->on_update(*this);
    }
}

auto entity_t::monitor(entities_monitor_t *monitor_) noexcept -> monitor_guard_t {
    this->entities_monitor = monitor_;
    return monitor_guard_t(this);
}

auto entity_t::get_presences() const noexcept -> const presences_t & { return presences; }

const entity_stats_t &entity_t::get_stats() noexcept { return statistics; }

auto entity_t::get_path() const noexcept -> const model::path_ptr_t & { return path; }

auto entity_t::get_children() noexcept -> children_t & { return children; }

using nc_t = entity_t::name_comparator_t;

bool nc_t::operator()(const entity_t *lhs, const entity_t *rhs) const noexcept {
    return lhs->get_path()->get_own_name() < rhs->get_path()->get_own_name();
}

bool nc_t::operator()(const entity_t *lhs, const std::string_view rhs) const noexcept {
    return lhs->get_path()->get_own_name() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const entity_t *rhs) const noexcept {
    return lhs < rhs->get_path()->get_own_name();
}

using mg_t = entity_t::monitor_guard_t;

mg_t::monitor_guard_t(entity_t *entity_) noexcept : entity{entity_} {}

mg_t::monitor_guard_t(monitor_guard_t &&other) noexcept : entity{nullptr} { std::swap(entity, other.entity); }

mg_t::~monitor_guard_t() {
    if (entity) {
        entity->entities_monitor = {};
    }
}
