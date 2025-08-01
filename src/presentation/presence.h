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

struct file_entity_t;
struct cluster_file_presence_t;

struct SYNCSPIRIT_API presence_t : model::proxy_t {
    using children_t = std::vector<presence_t *>;

    // clang-format off
    enum features_t: std::uint32_t {
        folder          = 1 << 0,
        file            = 1 << 1,
        directory       = 1 << 2,
        missing         = 1 << 3,
        cluster         = 1 << 4,
        peer            = 1 << 5,
        local           = 1 << 6,
        deleted         = 1 << 7,
        ignored         = 1 << 8,
        symblink        = 1 << 9,
        conflict        = 1 << 10,
    };
    static constexpr std::uint32_t mask = 0xFFFFFFFF;
    // clang-format ON

    presence_t(entity_t *entity, model::device_t* device) noexcept ;
    ~presence_t();

    presence_t *get_parent() noexcept ;
    entity_t* get_entity() noexcept;
    presence_t* set_parent(entity_t *entity, presence_t *parent = nullptr) noexcept;
    std::uint32_t get_features() const noexcept ;
    virtual const presence_stats_t& get_stats(bool sync = true) const noexcept ;
    const presence_stats_t& get_own_stats() const noexcept;
    inline const model::device_t* get_device() const noexcept { return device; }

    virtual const presence_t* determine_best(const presence_t*) const;

    children_t& get_children() noexcept;
    void clear_children() noexcept;

    static bool compare(const presence_t *l, const presence_t *r) noexcept;

    bool is_unique() const noexcept;

  protected:
    friend struct entity_t;
    friend struct cluster_file_presence_t;
    friend struct file_entity_t;

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void clear_presense() noexcept;
    void link(augmentable_t* augmentable) noexcept;
    void sync_with_entity() const noexcept;

    entity_t *entity;
    presence_t *parent;
    model::device_t* device;
    children_t children;
    mutable std::uint32_t features = 0;
    mutable std::uint32_t entity_generation = 0;
    mutable presence_stats_t statistics;
    mutable presence_stats_t own_statistics;

};

} // namespace syncspirit::presentation
