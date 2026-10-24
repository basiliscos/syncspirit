// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "misc/stats.h"
#include "device.h"
#include "syncspirit-export.h"
#include <cstdint>

namespace syncspirit::model {

struct entity_t;
using entity_ptr_t = model::intrusive_ptr_t<entity_t>;

struct presence_t;
using presence_ptr_t = model::intrusive_ptr_t<presence_t>;

struct file_entity_t;
struct cluster_file_presence_t;

struct folder_info_iterator_t;

presence_t *get_child(presence_t *parent, std::string_view name, bool is_dir) noexcept;

struct SYNCSPIRIT_API presence_t : model::augmentable_t {
    using children_t = std::vector<presence_t *>;

    // clang-format off
    enum features_t: std::uint32_t {
        file             = 1 << 0,
        directory        = 1 << 1,
        symblink         = 1 << 2,
        folder           = 1 << 3,
        missing          = 1 << 4,
        cluster          = 1 << 5,
        peer             = 1 << 6,
        local            = 1 << 7,
        deleted          = 1 << 8,
        ignored          = 1 << 9,
        unreachable      = 1 << 10,
        invalid          = 1 << 11,
        no_perms         = 1 << 12,
        synchronizing    = 1 << 13,
        available        = 1 << 14,
        normalized       = 1 << 15,
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

    children_t& get_children() noexcept;
    presence_t *get_child(std::string_view name, bool is_dir) noexcept ;
    presence_t *get_sibling(device_t* device) noexcept;
    void remove_child(const presence_t &) noexcept;
    void add_child(presence_t*, std::string_view name) noexcept;
    void forget_presence() noexcept;

    struct SYNCSPIRIT_API presence_like_t {
        std::string_view name;
        bool is_dir;
    };

    struct SYNCSPIRIT_API child_comparator_t {
        bool operator()(const presence_t *l, const presence_t *r) const;
        bool operator()(const presence_t *l, const presence_like_t& r) const;
    };

    bool is_unique() const noexcept;

  protected:
    friend struct entity_t;
    friend struct cluster_file_presence_t;
    friend struct file_entity_t;
    friend struct folder_info_iterator_t;

    void forget_entity() noexcept;
    void normalize() noexcept;

    template<typename Augmentable>
    void link(Augmentable *augmentable) noexcept { augmentable->set_augmentation(this); }
    void sync_with_entity() const noexcept;

    entity_t *entity;
    presence_t *parent;
    device_t* device;
    children_t children_;
    mutable std::uint32_t features = 0;
    mutable std::uint32_t entity_generation = 0;
    mutable presence_stats_t statistics;
    mutable presence_stats_t own_statistics;
};

} // namespace syncspirit::presentation
