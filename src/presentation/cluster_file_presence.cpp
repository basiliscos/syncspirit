// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "cluster_file_presence.h"
#include "file_entity.h"
#include "presence.h"
#include "model/file_info.h"
#include "model/folder_info.h"
#include "model/misc/resolver.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

cluster_file_presence_t::cluster_file_presence_t(std::uint32_t default_features_, file_entity_t &entity,
                                                 model::file_info_t &file_info_) noexcept
    : file_presence_t(&entity, file_info_.get_folder_info()->get_device()), file_info{file_info_},
      default_features{default_features_} {
    link(&file_info);
    statistics = own_statistics = refresh_own_stats();
    refresh_features();
}

void cluster_file_presence_t::refresh_features() noexcept {
    features = default_features | features_t::cluster;
    features |= (file_info.is_dir() ? features_t::directory : features_t::file);
    if (file_info.is_deleted()) {
        features |= features_t::deleted;
    }
    if (file_info.is_link()) {
        features |= features_t::symblink;
    }
}

auto cluster_file_presence_t::get_file_info() noexcept -> model::file_info_t & { return file_info; }

const presence_t *cluster_file_presence_t::determine_best(const presence_t *other) const {
    if (!(other->get_features() & features_t::cluster)) {
        return this;
    }
    auto o = static_cast<const cluster_file_presence_t *>(other);
    auto r = model::compare(file_info, o->file_info);
    return r >= 0 ? this : o;
}

presence_stats_t cluster_file_presence_t::refresh_own_stats() noexcept { return {1, file_info.get_size(), 0}; }

void cluster_file_presence_t::on_update() noexcept {
    refresh_features();
    auto presence_diff = refresh_own_stats() - own_statistics;
    auto entity_stats = entity->get_stats();
    auto device = file_info.get_folder_info()->get_device();
    auto prev_best = entity->best;
    auto new_best = entity->recalc_best();
    auto best_changed = prev_best != new_best && new_best->device == device;
    auto best_updated = !best_changed && device == new_best->device;
    entity->push_stats(presence_diff, device, best_updated);
    own_statistics += presence_diff;
    if (best_changed) {
        assert(new_best == this);
        auto entity_diff = get_stats(true) - presence_stats_t{entity_stats, 0};
        entity->push_stats(entity_diff, {}, true);
    }
}
