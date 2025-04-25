// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include <cassert>
#include <utility>

using namespace syncspirit;
using namespace syncspirit::presentation;

entity_t::entity_t(path_t path_, entity_t *parent_) noexcept
    : parent{parent_}, path(std::move(path_)), has_dir{false}, cluster_record{-1} {}

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
    for (auto &r : records) {
        r.presence->set_parent(value);
    }
}

void entity_t::push_stats(const presence_stats_t &diff, const model::device_t *source, bool best) noexcept {
    auto current = this;
    while (current) {
        if (best) {
            current->statistics += diff;
            ++current->generation;
        }
        for (auto &r : current->records) {
            if (r.device == source) {
                r.presence->statistics += diff;
                r.presence->entity_generation--;
                break;
            }
        }
        current = current->parent;
    }
}

void entity_t::remove_presense(presence_t &item) noexcept {
    auto predicate = [&item](const record_t &record) { return record.presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    if (children.size()) {
        bool rescan_children = true;
        while (rescan_children) {
            bool scan = true;
            for (auto it = children.begin(); scan && it != children.end();) {
                auto &c = *it;
                bool advance_it = true;
                for (auto &r : c->records) {
                    if (r.device == item.device) {
                        r.presence->clear_presense();
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
    if (best_device && it->device == best_device) {
        stats -= it->presence->get_stats(false);
        need_restat = true;
    }
    records.erase(it);
    if (need_restat) {
        if (auto best = recalc_best(); best) {
            stats += best->get_stats();
        }
    }
    if (stats != statistics) {
        auto diff = stats - statistics;
        push_stats({diff, 0}, item.device.get(), true);
    }

    bool remove_self = records.empty() && parent;
    if (records.size() == 1) {
        auto &r = records.front();
        if (!r.device) {
            remove_presense(*r.presence);
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
    best_device.reset();
    if (children.empty()) {
        auto &first = records.front();
        best_device = first.device;
        best = first.presence;
        for (size_t i = 1; i < records.size(); ++i) {
            auto &[device, presence, _] = records[i];
            best = presence->determine_best(best);
            if (best == presence) {
                best_device = device;
            }
        }
    }
    return best;
}

presence_t *entity_t::get_presence(model::device_t &device) noexcept {
    presence_t *fallback = nullptr;
    for (auto &record : records) {
        auto d = record.device.get();
        if (d == &device) {
            return record.presence;
        } else if (!d) {
            fallback = record.presence;
        }
    }
    return fallback;
}

void entity_t::on_update() noexcept { notify_update(); }
void entity_t::on_delete() noexcept { clear_children(); }

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

    for (auto &r : records) {
        auto p = r.presence->parent;
        while (p) {
            p->statistics -= r.presence->statistics;
            p = p->parent;
        }
        r.child_presences.clear();
    }
    child.parent = nullptr;
    push_stats({-child.get_stats(), 0}, nullptr, true);
    children.erase(it);
}

const entity_stats_t &entity_t::get_stats() noexcept { return statistics; }

void entity_t::commit(const path_t &path) noexcept {
    for (auto child : children) {
        child->commit(path);
        statistics += child->get_stats();
    }
    if (records.size()) {
        auto &first = records.front();
        for (auto &[device, presence, child_presences] : records) {
            auto parent = presence->parent;
            if (parent && path.contains(parent->entity->path)) {
                parent->statistics += presence->statistics;
            }
        }
    }
    if (children.empty()) {
        statistics = recalc_best()->get_stats();
    } else {
        if (parent) { // for file/dir entity
            ++statistics.entities;
        }
    }
    ++generation;
}

auto entity_t::get_child_presences(model::device_t &device) noexcept -> child_presences_t & {
    for (auto &r : records) {
        if (r.device == &device) {
            auto &presences = r.child_presences;
            actualize_on_demand(presences, device);
            return presences;
        }
    }
    assert(0 && "should not happen");
}

void entity_t::actualize_on_demand(child_presences_t &r, model::device_t &device) noexcept {
    if (r.size() != children.size()) {
        r.clear();
        r.reserve(children.size());
        for (auto &c : children) {
            auto p = c->get_presence(device);
            r.emplace_back(p);
        }
        auto comparator = [](const presence_t *l, const presence_t *r) {
            using F = presence_t::features_t;
            auto ld = l->get_features() & F::directory;
            auto rd = r->get_features() & F::directory;
            if (ld && !rd) {
                return true;
            } else if (!rd && !rd) {
                return false;
            }
            auto l_name = l->entity->get_path().get_own_name();
            auto r_name = r->entity->get_path().get_own_name();
            return l_name < r_name;
        };
        std::sort(r.begin(), r.end(), comparator);
    }
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
