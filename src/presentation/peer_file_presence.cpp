// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "peer_file_presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

peer_file_presence_t::peer_file_presence_t(file_entity_t &entity, model::file_info_t &file_info_) noexcept
    : cluster_file_presence_t(entity, file_info_) {
    features = features_t::cluster | features_t::peer | features_t::file;
}
