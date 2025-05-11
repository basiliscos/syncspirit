// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "file_presence.h"
#include "syncspirit-export.h"
#include <set>

namespace syncspirit::presentation {

struct file_entity_t;

struct SYNCSPIRIT_API missing_file_presence_t final : file_presence_t {
    using augmentations_t = std::set<model::augmentation_t *>;

    missing_file_presence_t(file_entity_t &entity) noexcept;
    ~missing_file_presence_t();

    void add(model::augmentation_t *) noexcept;
    void remove(model::augmentation_t *) noexcept;

    void on_update() noexcept override;
    void on_delete() noexcept override;

  private:
    augmentations_t augmentations;
};

} // namespace syncspirit::presentation
