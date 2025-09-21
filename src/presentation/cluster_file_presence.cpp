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

using F = presence_t::features_t;

cluster_file_presence_t::cluster_file_presence_t(std::uint32_t default_features_, file_entity_t &entity,
                                                 model::file_info_t &file_info_, const model::folder_info_t &folder_info) noexcept
    : file_presence_t(&entity, folder_info.get_device()), file_info{file_info_},
      default_features{default_features_} {
    link(&file_info);
    refresh_features();
    statistics = own_statistics = refresh_own_stats();
}

void cluster_file_presence_t::refresh_features() noexcept {
    features = default_features | F::cluster;
    features |= (file_info.is_dir() ? F::directory : F::file);
    if (file_info.is_deleted()) {
        features |= F::deleted;
    }
    if (file_info.is_link()) {
        features |= F::symblink;
    }
}

auto cluster_file_presence_t::get_file_info() const noexcept -> const model::file_info_t & { return file_info; }

const presence_t *cluster_file_presence_t::determine_best(const presence_t *other) const {
    if (!(other->get_features() & F::cluster)) {
        return this;
    }
    auto o = static_cast<const cluster_file_presence_t *>(other);
    auto r = model::compare(file_info, o->file_info);
    return r >= 0 ? this : o;
}

presence_stats_t cluster_file_presence_t::refresh_own_stats() noexcept {
    std::int64_t size;
    std::int32_t local;
    if (features & F::local) {
        if (file_info.is_local() && file_info.is_locally_available()) {
            local = 1;
            size = file_info.get_size();
        } else {
            local = 0;
            size = 0;
        }
    } else {
        local = 0;
        size = file_info.get_size();
    }
    return {size, 1, 0, local};
}

void cluster_file_presence_t::on_update() noexcept {
    refresh_features();
    auto ex_stats = own_statistics;
    auto presence_diff = refresh_own_stats() - ex_stats;
    auto entity_stats = entity->get_stats();
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
    file_presence_t::on_update();
}
