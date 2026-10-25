// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include "folder.h"
#include "cluster.h"
#include "utils/path_view.hpp"
#include "entity/file.h"
#include <cassert>
#include <memory_resource>

using namespace syncspirit::model;

using F = presence_t::features_t;

entity_t::entity_t(utils::path_ptr_t path_, entity_t *parent_) noexcept
    : parent{parent_}, path(std::move(path_)), best{nullptr}, entities_monitor{nullptr} {}

entity_t::~entity_t() {
    clear_children();
    for (auto &p : presences) {
        p->forget_entity();
    }
    presences.clear();
}

auto entity_t::get_loader() noexcept -> bulk_loader_t { return bulk_loader_t(*this); }

void entity_t::clear_children() noexcept {
    for (auto it = children.begin(); it != children.end(); ++it) {
        it->get()->forget_parent();
    }
    children.clear();
}

void entity_t::forget_parent() noexcept {
    parent = nullptr;
}
void entity_t::set_parent(entity_t *value) noexcept {
    parent = value;
    for (auto presence : presences) {
        presence->set_parent(value);
    }
}

void entity_t::add_presense(presence_t &item) noexcept {
    assert(std::find(presences.begin(), presences.end(), &item) == presences.end());
    presences.push_back(&item);
}

void entity_t::remove_presense(presence_t &item) noexcept {
    auto it = std::find(presences.begin(), presences.end(), &item);
    assert(it != presences.end());
    item.forget_entity();
    presences.erase(it);
}

presence_t *entity_t::get_presence(const model::device_t *device) noexcept {
    presence_t *r = nullptr;
    for (auto p : presences) {
        if (p->device == device) {
            return p.get();
        } else if (!p->device) {
            r = p.get();
        }
    }
    return r;
}

void entity_t::add_child(entity_t &child) noexcept {
    children.emplace(&child);
    child.set_parent(this);
}

void entity_t::commit(const utils::path_t &path, const model::device_t *device) noexcept {
    // TODO
}

auto entity_t::get_presences() const noexcept -> const presences_t & { return presences; }

const entity_stats_t &entity_t::get_stats() noexcept { return statistics; }

auto entity_t::get_path() const noexcept -> const utils::path_ptr_t & { return path; }

auto entity_t::get_children() noexcept -> children_t & { return children; }

entity_t* entity_t::get_or_create_child(std::string_view name) noexcept {
    auto it = children.find(name);
    if (it == children.end()) {
        auto new_child = create_child(name);
        it = children.insert(std::move(new_child)).first;
    }
    return it->get();
}

cluster_t& entity_t::get_cluster() noexcept {
    auto e = this;
    while (e->parent) {
        e = e->parent;
    }
    auto f = static_cast<folder_t*>(e);
    return *f->get_cluster();
}

entity_ptr_t entity_t::create_child(std::string_view name) noexcept {
   auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto view = path->get_view(allocator);
    auto path = view / utils::make_generic_view(name, allocator);
    auto& path_cache = get_cluster().get_path_cache();
    auto path_ptr = path_cache.get_path(path.get_full_name());
    return new entity::file_t(std::move(path_ptr), this);
}

using nc_t = entity_t::name_comparator_t;

bool nc_t::operator()(const entity_ptr_t &lhs, const entity_ptr_t &rhs) const noexcept {
    return lhs->get_path()->get_filename() < rhs->get_path()->get_filename();
}

bool nc_t::operator()(const entity_ptr_t &lhs, const std::string_view rhs) const noexcept {
    return lhs->get_path()->get_filename() < rhs;
}

bool nc_t::operator()(const std::string_view lhs, const entity_ptr_t &rhs) const noexcept {
    return lhs < rhs->get_path()->get_filename();
}
using bl_t = entity_t::bulk_loader_t;

bl_t &bl_t::add(presence_t *presence) noexcept { return *this; }

bl_t::~bulk_loader_t() {
    //
}
