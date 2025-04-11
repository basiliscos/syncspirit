// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/proxy.h"
#include "model/device.h"
#include "statistics.h"
#include "syncspirit-export.h"

namespace syncspirit::presentation {

struct entity_t;
using entity_ptr_t = model::intrusive_ptr_t<entity_t>;

struct presence_t;
using presence_ptr_t = model::intrusive_ptr_t<presence_t>;

struct SYNCSPIRIT_API presence_t : model::proxy_t {
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

    presence_t(entity_t *entity, model::device_ptr_t device) noexcept ;
    ~presence_t();

    presence_t *get_parent() noexcept ;
    void set_parent(presence_t *value) noexcept ;
    void set_parent(entity_t *entity) noexcept ;
    std::uint32_t get_presence_feautres() const noexcept ;
    const statistics_t& get_stats() const noexcept ;

    virtual const presence_t* determine_best(const presence_t*) const;
    void commit() noexcept;

  protected:
    void on_delete() noexcept override;
    void clear_presense() noexcept;
    void link(augmentable_t* augmentable) noexcept;

    entity_t *entity;
    presence_t *parent;
    augmentable_t* augmentable;
    model::device_ptr_t device;
    std::uint32_t features = 0;
    statistics_t statistics;
};

} // namespace syncspirit::presentation
