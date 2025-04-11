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

struct file_entity_t;
struct folder_entity_t;

struct SYNCSPIRIT_API entity_t : model::proxy_t {
    struct name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const entity_t *lhs, const entity_t *rhs) const noexcept;
        bool operator()(const entity_t *lhs, const std::string_view rhs) const noexcept;
        bool operator()(const std::string_view lhs, const entity_t *rhs) const noexcept;
    };
    struct string_comparator_t : std::less<void> {
        using is_transparent = std::true_type;
    };
    using children_t = std::set<entity_t *, name_comparator_t>;

    entity_t(path_t path, entity_t *parent = nullptr) noexcept;
    virtual ~entity_t();
    const path_t &get_path() const noexcept;

    template <typename Presence>
        requires std::is_base_of<presence_t, Presence>::value
    Presence *get_presense(model::device_t &device) noexcept {
        return static_cast<Presence *>(get_presense_raw(device));
    }

    children_t &get_children() noexcept;
    entity_t *get_parent() noexcept;
    void add_child(entity_t &child) noexcept;
    void remove_child(entity_t &child) noexcept;
    void remove_presense(presence_t &) noexcept;
    const statistics_t &get_stats() noexcept;

  protected:
    friend struct file_entity_t;
    friend struct folder_entity_t;

    struct record_t {
        model::device_ptr_t device;
        presence_t *presence;
    };
    using records_t = std::vector<record_t>;

    presence_t *get_presense_raw(model::device_t &device) noexcept;

    void clear_children() noexcept;

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void set_parent(entity_t *parent) noexcept;
    void commit() noexcept;
    void push_stats(const statistics_t &diff) noexcept;
    const presence_t *recalc_best() noexcept;

    entity_t *parent;
    records_t records;
    path_t path;
    children_t children;
    statistics_t statistics;
    int cluster_record;
    model::device_ptr_t best_device;
};

} // namespace syncspirit::presentation
