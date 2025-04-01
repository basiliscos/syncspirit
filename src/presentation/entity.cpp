// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"

#include "model/folder.h"

#include <cassert>

using namespace syncspirit;
using namespace syncspirit::presentation;

entity_t::entity_t(entity_t *parent_) : parent{parent_} {}

entity_t::~entity_t() {}

std::string_view entity_t::get_name() const { return name; }

auto entity_t::get_children() -> children_t & { return children; }

void entity_t::remove_presense(presence_t &item) {
    auto predicate = [&item](const record_t &record) { return record.presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    records.erase(it);
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
