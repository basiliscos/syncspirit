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
    statistics = get_own_stats();
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

statistics_t cluster_file_presence_t::get_own_stats() const noexcept { return {1, file_info.get_size()}; }

void cluster_file_presence_t::on_update() noexcept {
    refresh_features();
    auto presence_diff = get_own_stats() - statistics;
    auto entity_stats = entity->get_stats();
    auto device = file_info.get_folder_info()->get_device();
    auto best_device = entity->best_device.get();
    auto best_presence = entity->recalc_best();
    auto best_changed = entity->best_device != best_device && entity->best_device == device;
    auto best_updated = !best_changed && device == entity->best_device;
    entity->push_stats(presence_diff, device, best_updated);
    if (best_changed) {
        assert(best_presence == this);
        auto entity_diff = get_stats() - entity_stats;
        entity->push_stats(entity_diff, {}, true);
    }
}
