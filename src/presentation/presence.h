// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/arc.hpp"
#include "model/device.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct entity_t;
using entity_ptr_t = model::intrusive_ptr_t<entity_t>;

struct presence_t;
using presence_ptr_t = model::intrusive_ptr_t<presence_t>;

struct SYNCSPIRIT_API presence_t : virtual model::augmentable_t<entity_t>, protected virtual model::augmentation_t {
    // clang-format off
    enum features_t: std::uint32_t {
        file    = 1 << 1,
        folder  = 1 << 2,
        missing = 1 << 3,
        cluster = 1 << 4,
        peer    = 1 << 5,
        local   = 1 << 6,
        deleted = 1 << 7,
        ignored = 1 << 8,
    };
    // clang-format ON

    presence_t(entity_t *entity, model::device_ptr_t device);
    ~presence_t();

    void set_parent(presence_t *value);
    presence_t *get_parent();
    void set_parent(entity_t *entity);
    std::uint32_t get_presence_feautres();

  protected:
    entity_t *entity;
    presence_t *parent;
    model::device_ptr_t device;
    std::uint32_t features = 0;
};

} // namespace syncspirit::presentation
