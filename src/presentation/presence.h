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
    presence_t(entity_t &entity, model::device_ptr_t device);
    ~presence_t();

  protected:
    entity_t &entity;
    presence_t *parent;
    model::device_ptr_t device;
};

} // namespace syncspirit::presentation
