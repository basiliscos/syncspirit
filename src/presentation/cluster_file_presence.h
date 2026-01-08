// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file_presence.h"
#include "syncspirit-export.h"
#include "model/file_info.h"

namespace syncspirit::presentation {

struct file_entity_t;
struct folder_presence_t;

struct SYNCSPIRIT_API cluster_file_presence_t : file_presence_t {
    cluster_file_presence_t(std::uint32_t default_features, file_entity_t &entity, model::file_info_t &file_info,
                            const model::folder_info_t &folder_info) noexcept;

    const model::file_info_t &get_file_info() const noexcept;
    const presence_t *determine_best(const presence_t *) const override;
    const folder_presence_t *get_folder() const noexcept;

  protected:
    void on_update() noexcept override;
    void refresh_features() noexcept;
    presence_stats_t refresh_own_stats() noexcept;

    model::file_info_t &file_info;
    std::uint32_t default_features = 0;
};

} // namespace syncspirit::presentation
