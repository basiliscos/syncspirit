// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#pragma once

#include "model/misc/stats.h"
#include "utils/path.h"
#include "model/device.h"
#include "syncspirit-export.h"

#include <string_view>
#include <set>
#include <type_traits>
#include <vector>

namespace syncspirit::model {

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
    struct SYNCSPIRIT_API name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const entity_ptr_t &lhs, const entity_ptr_t &rhs) const noexcept;
        bool operator()(const entity_ptr_t &lhs, const std::string_view rhs) const noexcept;
        bool operator()(const std::string_view lhs, const entity_ptr_t &rhs) const noexcept;
    };
    struct SYNCSPIRIT_API bulk_loader_t {
        inline bulk_loader_t(entity_t &entity_) noexcept : entity{entity_} {}
        bulk_loader_t &add(presence_t *presence) noexcept;
        ~bulk_loader_t();

      private:
        entity_t &entity;
    };

    using children_t = std::set<entity_ptr_t, name_comparator_t>;
    using presences_t = std::vector<presence_ptr_t>;

    entity_t(utils::path_ptr_t path, entity_t *parent = nullptr) noexcept;
    virtual ~entity_t();

    bulk_loader_t get_loader() noexcept;

    const utils::path_ptr_t &get_path() const noexcept;

    presence_t *get_presence(const model::device_t *device) noexcept;

    children_t &get_children() noexcept;

    inline entity_t *get_parent() noexcept { return parent; }
    inline const entity_t *get_parent() const noexcept { return parent; }
    inline const presence_t *get_best() const noexcept { return best; }

    void add_child(entity_t &child) noexcept;
    entity_t* get_or_create_child(std::string_view name) noexcept;
    void add_presense(presence_t &) noexcept;
    void remove_presense(presence_t &) noexcept;
    const entity_stats_t &get_stats() noexcept;
    const presences_t &get_presences() const noexcept;

  protected:
    friend struct bulk_loader_t;
    friend struct presence_t;
    friend struct file_entity_t;
    friend struct folder_entity_t;
    friend struct cluster_file_presence_t;

    void forget_parent() noexcept;
    void clear_children() noexcept;
    void set_parent(entity_t *parent) noexcept;
    void commit(const utils::path_t &path, const model::device_t *device) noexcept;
    cluster_t& get_cluster() noexcept;
    virtual entity_ptr_t create_child(std::string_view name) noexcept;

    inline entities_monitor_t *get_monitor() noexcept {
        auto e = this;
        while (e->parent) {
            e = e->parent;
        }
        return e->entities_monitor;
    }


    entity_t *parent;
    presences_t presences;
    utils::path_ptr_t path;
    children_t children;
    entity_stats_t statistics;
    const presence_t *best;
    entities_monitor_t *entities_monitor;
};

} // namespace syncspirit::model
