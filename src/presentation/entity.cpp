// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include <cassert>

using namespace syncspirit;
using namespace syncspirit::presentation;

entity_t::entity_t(path_t path_, entity_t *parent_) noexcept
    : parent{parent_}, path(std::move(path_)), cluster_record{-1} {}

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

void entity_t::remove_presense(presence_t &item) noexcept {
    auto predicate = [&item](const record_t &record) { return record.presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    records.erase(it);

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

presence_t *entity_t::get_presense_raw(model::device_t &device) noexcept {
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
    child.set_parent(this);
    model::intrusive_ptr_add_ref(&child);
    children.emplace(&child);
}

void entity_t::remove_child(entity_t &child) noexcept {
    auto it = children.equal_range(&child).first;
    assert(it != children.end());
    child.clear_children();
    child.set_augmentation({});
    model::intrusive_ptr_release(&child);
    child.parent = nullptr;
    children.erase(it);
}

const statistics_t &entity_t::get_stats() noexcept { return statistics; }

void entity_t::commit() noexcept {
    for (auto child : children) {
        child->commit();
        statistics += child->get_stats();
    }
    auto best = const_cast<const presence_t *>(records.front().presence);
    for (auto &[_, presence] : records) {
        presence->commit();
        best = presence->determine_best(best);
    }
    if (children.empty()) {
        statistics = best->get_stats();
    } else {
        if (parent) { // for file/dir entity
            ++statistics.entities;
        }
    }
}

using nc_t = entity_t::name_comparator_t;

bool nc_t::operator()(const entity_t *lhs, const entity_t *rhs) const noexcept {
    auto ld = lhs->children.size() > 0;
    auto rd = rhs->children.size() > 0;
    if (ld && !rd) {
        return true;
    } else if (rd && !ld) {
        return false;
    }
    return lhs->get_path().get_full_name() < rhs->get_path().get_full_name();
}

bool nc_t::operator()(const entity_t *lhs, const std::string_view rhs) const noexcept {
    auto ld = lhs->children.size() > 0;
    if (!ld) {
        return false;
    }
    return lhs->get_path().get_full_name() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const entity_t *rhs) const noexcept {
    auto rd = rhs->children.size() > 0;
    if (!rd) {
        return true;
    }
    return lhs < rhs->get_path().get_full_name();
}
