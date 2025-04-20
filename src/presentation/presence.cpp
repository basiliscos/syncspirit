// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

presence_t::presence_t(entity_t *entity_, model::device_ptr_t device_) noexcept
    : entity{entity_}, device{std::move(device_)}, parent{nullptr}, augmentable{nullptr} {
    if (entity) {
        model::intrusive_ptr_add_ref(entity);
    }
}

presence_t::~presence_t() { clear_presense(); }

presence_t *presence_t::get_parent() noexcept { return parent; }
entity_t *presence_t::get_entity() noexcept { return entity; }

auto presence_t::get_features() const noexcept -> std::uint32_t { return features; }

const statistics_t &presence_t::get_stats() const noexcept { return statistics; }

void presence_t::set_parent(entity_t *value) noexcept {
    if (value && device) {
        parent = value->get_presence(*device);
    }
}

void presence_t::set_parent(presence_t *value) noexcept {
    assert(entity->get_parent() == value->entity);
    parent = value;
}

void presence_t::clear_presense() noexcept {
    if (augmentable) {
        augmentable->set_augmentation({});
    }
    if (entity) {
        entity->remove_presense(*this);
        model::intrusive_ptr_release(entity);
        entity = nullptr;
    }
}

void presence_t::link(augmentable_t *augmentable) noexcept { augmentable->set_augmentation(this); }

void presence_t::on_delete() noexcept { clear_presense(); }

const presence_t *presence_t::determine_best(const presence_t *other) const { return other; }

statistics_t presence_t::get_own_stats() const noexcept { return {0, 0}; }
