// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/augmentation.h"
#include "model/device.h"
#include "path.h"
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

struct folder_entity_t;

struct SYNCSPIRIT_API entity_t : virtual model::augmentable_t, protected virtual model::augmentation_t {
    struct name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const entity_t *lhs, const entity_t *rhs) const;
        bool operator()(const entity_t *lhs, const std::string_view rhs) const;
        bool operator()(const std::string_view lhs, const entity_t *rhs) const;
    };
    struct string_comparator_t : std::less<void> {
        using is_transparent = std::true_type;
    };
    using children_t = std::set<entity_t *, name_comparator_t>;

    entity_t(path_t path, entity_t *parent = nullptr);
    virtual ~entity_t();
    const path_t &get_path() const;

    template <typename Presence>
        requires std::is_base_of<presence_t, Presence>::value
    Presence *get_presense(model::device_t &device) {
        return static_cast<Presence *>(get_presense_raw(device));
    }

    children_t &get_children();
    entity_t *get_parent();
    void add_child(entity_t &child);
    void remove_child(entity_t &child);
    void remove_presense(presence_t &);

  protected:
    friend struct folder_entity_t;

    struct record_t {
        model::device_ptr_t device;
        presence_t *presence;
    };
    using records_t = std::vector<record_t>;

    presence_t *get_presense_raw(model::device_t &device);

    void clear_children();

    void on_update() noexcept override;
    void on_delete() noexcept override;
    void set_parent(entity_t *parent);

    entity_t *parent;
    records_t records;
    path_t path;
    children_t children;
};

} // namespace syncspirit::presentation
