// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

presence_t::presence_t(entity_t *entity_, model::device_ptr_t device_)
    : entity{entity_}, device{std::move(device_)}, parent{nullptr} {
    if (entity) {
        model::intrusive_ptr_add_ref(entity);
    }
}

presence_t::~presence_t() { clear_presense(); }

presence_t *presence_t::get_parent() { return parent; }

auto presence_t::get_presence_feautres() -> std::uint32_t { return features; }

void presence_t::set_parent(entity_t *value) {
    if (value && device) {
        parent = value->get_presense<presence_t>(*device);
    }
}

void presence_t::clear_presense() noexcept {
    if (entity) {
        entity->remove_presense(*this);
        model::intrusive_ptr_release(entity);
        entity = nullptr;
    }
}

void presence_t::on_delete() noexcept { clear_presense(); }
