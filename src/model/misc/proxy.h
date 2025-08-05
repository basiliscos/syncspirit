// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "augmentation.h"
#include "syncspirit-export.h"

namespace syncspirit::model {

struct SYNCSPIRIT_API proxy_t : model::augmentable_t, model::augmentation_t {};

} // namespace syncspirit::model
