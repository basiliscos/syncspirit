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

cluster_file_presence_t::cluster_file_presence_t(file_entity_t &entity, model::file_info_t &file_info_) noexcept
    : file_presence_t(&entity, file_info_.get_folder_info()->get_device()), file_info{file_info_} {
    link(&file_info);
    statistics = {1, file_info.get_size()};
}

auto cluster_file_presence_t::get_file_info() noexcept -> model::file_info_t & { return file_info; }

const presence_t *cluster_file_presence_t::determine_best(const presence_t *other) const {
    if (!(other->get_presence_feautres() & features_t::cluster)) {
        return this;
    }
    auto o = static_cast<const cluster_file_presence_t *>(other);
    auto r = model::compare(file_info, o->file_info);
    return r >= 0 ? this : o;
}
