// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include <cassert>

using namespace syncspirit;
using namespace syncspirit::presentation;

entity_t::entity_t(path_t path_, entity_t *parent_) : parent{parent_}, path(std::move(path_)) {}

entity_t::~entity_t() {}

auto entity_t::get_path() const -> const path_t & { return path; }

auto entity_t::get_children() -> children_t & { return children; }

entity_t *entity_t::get_parent() { return parent; }

void entity_t::set_parent(entity_t *value) {
    parent = value;
    for (auto &r : records) {
        r.presence->set_parent(value);
    }
}

void entity_t::remove_presense(presence_t &item) {
    auto predicate = [&item](const record_t &record) { return record.presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    records.erase(it);

#if 0
    if (parent) {
        if (records.empty() || !records.front().device) {
            remove_child(*this);
        }
        parent = nullptr;
    }
#endif
}

presence_t *entity_t::get_presense_raw(model::device_t &device) {
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
void entity_t::on_delete() noexcept {}

void entity_t::add_child(entity_ptr_t child) {
    child->set_parent(this);
    children.emplace(std::move(child));
}

void entity_t::remove_child(entity_t &child) {
    auto it = children.equal_range(&child).first;
    children.erase(it);
}

using nc_t = entity_t::name_comparator_t;

bool nc_t::operator()(const entity_t *lhs, const entity_t *rhs) const {
    auto ld = lhs->children.size() > 0;
    auto rd = rhs->children.size() > 0;
    if (ld && !rd) {
        return true;
    } else if (rd && !ld) {
        return false;
    }
    return lhs->get_path().get_full_name() < rhs->get_path().get_full_name();
}

bool nc_t::operator()(const entity_ptr_t &lhs, const entity_ptr_t &rhs) const {
    return operator()(lhs.get(), rhs.get());
}

bool nc_t::operator()(const entity_ptr_t &lhs, const entity_t *rhs) const { return operator()(lhs.get(), rhs); }

bool nc_t::operator()(const entity_t *lhs, const std::string_view rhs) const {
    auto ld = lhs->children.size() > 0;
    if (!ld) {
        return false;
    }
    return lhs->get_path().get_full_name() < rhs;
}

bool nc_t::operator()(const entity_ptr_t &lhs, const std::string_view rhs) const { return operator()(lhs.get(), rhs); }

bool nc_t::operator()(const std::string_view lhs, const entity_t *rhs) const {
    auto rd = rhs->children.size() > 0;
    if (!rd) {
        return true;
    }
    return lhs < rhs->get_path().get_full_name();
}

bool nc_t::operator()(const std::string_view lhs, const entity_ptr_t &rhs) const { return operator()(lhs, rhs.get()); }
