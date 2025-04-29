// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"
#include "cluster_file_presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

presence_t::presence_t(entity_t *entity_, model::device_ptr_t device_) noexcept
    : entity{entity_}, device{std::move(device_)}, parent{nullptr}, augmentable{nullptr} {
    if (entity) {
        model::intrusive_ptr_add_ref(entity);
    }
}

presence_t::~presence_t() { clear_presense(); }

presence_t *presence_t::get_parent() noexcept { return parent; }
entity_t *presence_t::get_entity() noexcept { return entity; }

auto presence_t::get_features() const noexcept -> std::uint32_t { return features; }

const presence_stats_t &presence_t::get_stats(bool sync) const noexcept {
    if (sync) {
        sync_with_entity();
    }
    return statistics;
}

const presence_stats_t &presence_t::get_own_stats() const noexcept { return own_statistics; }

presence_t *presence_t::set_parent(entity_t *value) noexcept {
    if (value && device) {
        parent = value->get_presence(*device);
    }
    return parent;
}

void presence_t::set_parent(presence_t *value) noexcept {
    assert(entity->get_parent() == value->entity);
    parent = value;
}

void presence_t::clear_presense() noexcept {
    if (augmentable) {
        augmentable->set_augmentation({});
    }
    if (entity) {
        entity->remove_presense(*this);
        model::intrusive_ptr_release(entity);
        entity = nullptr;
    }
}

void presence_t::link(augmentable_t *augmentable) noexcept { augmentable->set_augmentation(this); }

void presence_t::on_delete() noexcept { clear_presense(); }

const presence_t *presence_t::determine_best(const presence_t *other) const { return other; }

void presence_t::sync_with_entity() const noexcept {
    static constexpr auto mask = features_t::cluster | features_t::folder;
    if (entity_generation != entity->generation && (features & mask)) {
        entity_generation = entity->generation;
        statistics.cluster_entries = 0;

        for (auto &child_entity : entity->get_children()) {
            auto child_presence = child_entity->get_presence(*device);
            if (child_presence->features & features_t::cluster) {
                auto child = static_cast<cluster_file_presence_t *>(child_presence);
                child->sync_with_entity();
                statistics.cluster_entries += child->statistics.cluster_entries;
            }
        }

        if (features & (features_t::file | features_t::directory)) {
            auto best_version = proto::Counter();
            for (auto [d, presence, _] : entity->records) {
                if ((d == entity->best_device) && (presence->get_features() & features_t::cluster)) {
                    auto best = static_cast<cluster_file_presence_t *>(presence);
                    best_version = best->get_file_info().get_version()->get_best();
                    break;
                }
            }
            assert(features & features_t::cluster);
            auto const_self = static_cast<const cluster_file_presence_t *>(this);
            auto self = const_cast<cluster_file_presence_t *>(const_self);
            if (self->get_file_info().get_version()->get_best() == best_version) {
                ++statistics.cluster_entries;
            }
        } else if (features & features_t::directory) {
            ++statistics.cluster_entries;
        }
    }
}
