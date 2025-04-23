// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file_presence.h"
#include "syncspirit-export.h"
#include "model/file_info.h"

namespace syncspirit::presentation {

struct file_entity_t;

struct SYNCSPIRIT_API cluster_file_presence_t : file_presence_t {
    cluster_file_presence_t(std::uint32_t default_features, file_entity_t &entity,
                            model::file_info_t &file_info) noexcept;

    model::file_info_t &get_file_info() noexcept;
    const presence_t *determine_best(const presence_t *) const override;

    presence_stats_t get_own_stats() const noexcept override final;
    const presence_stats_t &get_stats(bool sync) const noexcept override final;

  protected:
    void on_update() noexcept override;
    void refresh_features() noexcept;

    model::file_info_t &file_info;
    std::uint32_t default_features = 0;
};

} // namespace syncspirit::presentation
