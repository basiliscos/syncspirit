// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include "folder_info.h"
#include <spdlog/spdlog.h>
#include <algorithm>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__APPLE__)
#include <uni_algo/case.h>
#endif

#define NORMALIZE_IF_NEEDED()                                                                                          \
    if (!(features & F::normalized)) {                                                                                 \
        normalize();                                                                                                   \
    }

namespace syncspirit::model {

using F = presence_t::features_t;
using CP = presence_t::child_comparator_t;

bool CP::operator()(const presence_t *l, const presence_t *r) const {
    auto lf = l->get_features();
    if (lf & F::missing) {
        assert(l->entity->best);
        l = l->entity->best;
        lf = l->get_features();
    }
    auto rf = r->get_features();
    if (rf & F::missing) {
        assert(r->entity->best);
        r = r->entity->best;
        rf = r->get_features();
    }

    auto ld = lf & F::directory;
    auto rd = rf & F::directory;
    if (ld && !rd) {
        return true;
    } else if (!ld && rd) {
        return false;
    }
    auto l_name = l->entity->get_path()->get_filename();
    auto r_name = r->entity->get_path()->get_filename();
    return l_name < r_name;
}

bool CP::operator()(const presence_t *l, const presence_like_t &r) const {
    auto lf = l->get_features();
    if (lf & F::missing) {
        assert(l->entity->best);
        l = l->entity->best;
        lf = l->get_features();
    }
    auto ld = lf & F::directory;
    auto rd = r.is_dir;

    if (ld && !rd) {
        return true;
    } else if (!ld && rd) {
        return false;
    }
    return l->entity->get_path()->get_filename() < r.name;
}

file_info_t *get_child(presence_t *parent, std::string_view name, bool is_dir) noexcept {
    return parent ? static_cast<file_info_t *>(parent->get_child(name, is_dir)) : nullptr;
}

presence_t::presence_t(entity_t *entity_, model::device_t *device_) noexcept
    : entity{entity_}, device{device_}, parent{nullptr} {
    if (entity) {
        entity->add_presense(*this);
    }
}

presence_t::~presence_t() {}

presence_t *presence_t::get_parent() noexcept { return parent; }

entity_t *presence_t::get_entity() noexcept { return entity; }

auto presence_t::get_features() const noexcept -> std::uint32_t { return features; }

const presence_stats_t &presence_t::get_stats(bool sync) const noexcept {
    if (sync) {
        sync_with_entity();
    }
    return statistics;
}

void presence_t::sync_with_entity() const noexcept {
    // TODO
}

auto presence_t::get_children() noexcept -> children_t & {
    NORMALIZE_IF_NEEDED();
    return children;
}

void presence_t::normalize() noexcept {
    if (features & F::normalized) {
        return;
    }
    features |= F::normalized;
    if (!children.empty()) {
        std::sort(children.begin(), children.end(), child_comparator_t{});
    }
}
presence_t *presence_t::get_child(std::string_view name, bool is_dir) noexcept {
    NORMALIZE_IF_NEEDED();
    auto comparator = child_comparator_t{};
    auto presence_like = presence_t::presence_like_t{name, is_dir};
    auto &children = get_children();
    auto it = std::lower_bound(children.begin(), children.end(), presence_like, comparator);
    if (it != children.end()) {
        auto &p = *it;
        if (p->get_entity()->get_path()->get_filename() == name) {
            auto p_dir = (bool)(p->features & F::directory);
            if (!(p_dir xor is_dir)) {
                if (!(p->features & F::missing)) {
                    return p;
                }
            }
        }
    }
    return nullptr;
}

presence_t *presence_t::get_sibling(const device_t *device_) noexcept {
    assert(device_ != device);
    return entity->get_presence(device_);
}

void presence_t::forget_entity() noexcept { entity = nullptr; }

void presence_t::forget_presence() noexcept {
    for (auto &c : children) {
        c->forget_presence();
    }
    if (parent) {
        get_folder_info()->forget_file(*entity->get_path());
        parent->remove_child(*this);
    }
    if (entity) {
        entity->remove_presense(*this);
    }
}

auto presence_t::get_folder_info() noexcept -> folder_info_t * {
    auto p = this;
    while (p->parent) {
        p = p->parent;
    }
    assert(p->get_features() & F::folder);
    return static_cast<folder_info_t *>(p);
}

bool presence_t::is_unique() const noexcept {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) || defined(__APPLE__)
    using una::caseless::compare_utf8;
    if (!(features & F::deleted)) {
        if (parent && parent->entity) {
            auto my_name = entity->get_path()->get_filename();
            for (auto c : parent->get_children()) {
                if (c != this && !(c->features & F::deleted)) {
                    auto other_name = c->entity->get_path()->get_filename();
                    if (compare_utf8(my_name, other_name) == 0) {
                        return false;
                    }
                }
            }
        }
    }
#endif
    return true;
}

presence_t *presence_t::set_parent(entity_t *entity_, presence_t *parent_) noexcept {
    if (entity_) {
        entity = entity_;
        entity->add_presense(*this);
    }
    parent = parent_;
    return this;
}

void presence_t::add_child(presence_t *child, std::string_view name) noexcept {
    auto child_entity = entity->get_or_create_child(name);
    child->set_parent(child_entity, this);
    auto reset_normalization = false;
    if (children.size() > 0) {
        auto comparator = child_comparator_t{};
        reset_normalization = !comparator(children.back(), child);
    }
    children.push_back(std::move(child));
    if (reset_normalization) {
        features = features & ~F::normalized;
    }
}

void presence_t::remove_child(const presence_t &child) noexcept {
    // no need to bisect, as children are remove from tail
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (*it == &child) {
            children.erase(std::next(it).base());
            break;
        }
    }
}

void presence_t::reset_normalization() noexcept { features = features & ~F::normalized; }

} // namespace syncspirit::model
