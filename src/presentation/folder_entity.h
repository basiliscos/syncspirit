// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "entity.h"

namespace syncspirit::presentation {

struct folder_presence_t;

struct SYNCSPIRIT_API folder_entity_t : entity_t {
    folder_entity_t(model::folder_ptr_t folder);
    model::folder_t &get_folder();

  private:
    model::folder_t &folder;
};

using folder_entity_ptr_t = model::intrusive_ptr_t<folder_entity_t>;

} // namespace syncspirit::presentation
