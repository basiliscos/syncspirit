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
        folder          = 1 << 1,
        file            = 1 << 2,
        directory       = 1 << 3,
        missing         = 1 << 4,
        cluster         = 1 << 5,
        peer            = 1 << 6,
        local           = 1 << 7,
        deleted         = 1 << 8,
        ignored         = 1 << 9,
        symblink        = 1 << 10,
        in_sync         = 1 << 11,
        conflict        = 1 << 12,
    };
    static constexpr std::uint32_t mask = 0xFFFFFFFF;
    // clang-format ON

    presence_t(entity_t *entity, model::device_ptr_t device) noexcept ;
    ~presence_t();

    presence_t *get_parent() noexcept ;
    entity_t* get_entity() noexcept;
    void set_parent(presence_t *value) noexcept ;
    void set_parent(entity_t *entity) noexcept ;
    std::uint32_t get_features() const noexcept ;
    virtual const presence_stats_t& get_stats(bool sync = true) const noexcept ;
    inline model::device_t* get_device() const noexcept { return device.get() ;}

    virtual const presence_t* determine_best(const presence_t*) const;
    virtual presence_stats_t get_own_stats() const noexcept;

  protected:
    friend struct entity_t;
    void on_delete() noexcept override;
    void clear_presense() noexcept;
    void link(augmentable_t* augmentable) noexcept;

    entity_t *entity;
    presence_t *parent;
    augmentable_t* augmentable;
    model::device_ptr_t device;
    mutable std::uint32_t features = 0;
    mutable std::uint32_t entity_generation = 0;
    mutable presence_stats_t statistics;
};

} // namespace syncspirit::presentation
