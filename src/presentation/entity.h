// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/proxy.h"
#include "model/device.h"
#include "path.h"
#include "statistics.h"
#include "syncspirit-export.h"

#include <string_view>
#include <set>
#include <type_traits>
#include <vector>

namespace syncspirit::presentation {

struct entity_t;
using entity_ptr_t = model::intrusive_ptr_t<entity_t>;

struct presence_t;
using presence_ptr_t = model::intrusive_ptr_t<presence_t>;
struct cluster_file_presence_t;

struct file_entity_t;
struct folder_entity_t;

struct SYNCSPIRIT_API entities_monitor_t {
    virtual void on_update(const entity_t &entity) noexcept = 0;
    virtual void on_delete(entity_t &entity) noexcept = 0;
};

struct SYNCSPIRIT_API entity_t : model::augmentable_t {
    struct SYNCSPIRIT_API monitor_guard_t {
        monitor_guard_t(entity_t *entity) noexcept;
        monitor_guard_t(const monitor_guard_t &) = delete;
        monitor_guard_t(monitor_guard_t &&) = default;
        ~monitor_guard_t();

      private:
        entity_t *entity;
    };
    struct SYNCSPIRIT_API name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const entity_t *lhs, const entity_t *rhs) const noexcept;
        bool operator()(const entity_t *lhs, const std::string_view rhs) const noexcept;
        bool operator()(const std::string_view lhs, const entity_t *rhs) const noexcept;
    };
    struct string_comparator_t : std::less<void> {
        using is_transparent = std::true_type;
    };
    using children_t = std::set<entity_t *, name_comparator_t>;
    using child_presences_t = std::vector<presence_t *>;

    entity_t(path_t path, entity_t *parent = nullptr) noexcept;
    virtual ~entity_t();
    const path_t &get_path() const noexcept;

    presence_t *get_presence(model::device_t &device) noexcept;

    children_t &get_children() noexcept;
    entity_t *get_parent() noexcept;
    void add_child(entity_t &child) noexcept;
    void remove_child(entity_t &child) noexcept;
    void remove_presense(presence_t &) noexcept;
    const entity_stats_t &get_stats() noexcept;
    [[nodiscard]] monitor_guard_t monitor(entities_monitor_t *) noexcept;

  protected:
    friend struct presence_t;
    friend struct file_entity_t;
    friend struct folder_entity_t;
    friend struct cluster_file_presence_t;

    using records_t = std::vector<presence_t *>;

    void clear_children() noexcept;
    void set_parent(entity_t *parent) noexcept;
    void commit(const path_t &path, const model::device_t *device) noexcept;
    void push_stats(const presence_stats_t &diff, const model::device_t *source, bool best) noexcept;
    const presence_t *recalc_best() noexcept;

    inline entities_monitor_t *get_monitor() noexcept {
        auto e = this;
        while (e->parent) {
            e = e->parent;
        }
        return e->entities_monitor;
    }

    entity_t *parent;
    records_t records;
    path_t path;
    children_t children;
    entity_stats_t statistics;
    std::uint32_t generation = 0;
    const presence_t *best;
    entities_monitor_t *entities_monitor;
};

} // namespace syncspirit::presentation
