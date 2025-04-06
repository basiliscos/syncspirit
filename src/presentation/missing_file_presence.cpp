// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "missing_file_presence.h"
#include "file_entity.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

missing_file_presence_t::missing_file_presence_t(file_entity_t &entity_) noexcept : file_presence_t({}, {}) {
    features = features_t::missing | features_t::file;
    entity = &entity_;
}

missing_file_presence_t::~missing_file_presence_t() { entity = nullptr; }
