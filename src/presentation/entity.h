// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "model/misc/augmentation.hpp"
#include "model/device.h"
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

struct SYNCSPIRIT_API entity_t : virtual model::augmentable_t<entity_t>, protected virtual model::augmentation_t {
    struct name_comparator_t {
        using is_transparent = std::true_type;
        bool operator()(const entity_ptr_t &lhs, const entity_ptr_t &rhs) const;
        bool operator()(const entity_ptr_t &lhs, const std::string_view rhs) const;
        bool operator()(const std::string_view lhs, const entity_ptr_t &rhs) const;
    };

    using children_t = std::set<entity_ptr_t, name_comparator_t>;

    entity_t(entity_t *parent);
    virtual ~entity_t();
    virtual std::string_view get_name() const;

    template <typename Presence>
        requires std::is_base_of<presence_t, Presence>::value
    Presence *get_presense(model::device_t &device) {
        return static_cast<Presence *>(get_presense_raw(device));
    }

    children_t &get_children();
    void remove_child(entity_t &);
    void remove_presense(presence_t &);

  protected:
    struct record_t {
        model::device_ptr_t device;
        presence_t *presence;
    };
    using records_t = std::vector<record_t>;

    presence_t *get_presense_raw(model::device_t &device);

    void on_update() noexcept override;
    void on_delete() noexcept override;

    entity_t *parent;
    records_t records;
    std::string_view name;
    children_t children;
};

} // namespace syncspirit::presentation
