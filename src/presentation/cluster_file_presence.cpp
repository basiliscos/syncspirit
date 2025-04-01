// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "cluster_file_presence.h"
#include "file_entity.h"
#include "presence.h"
#include "model/file_info.h"
#include "model/folder_info.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

cluster_file_presence_t::cluster_file_presence_t(file_entity_t &entity, model::file_info_t &file_info_)
    : file_presence_t(entity, file_info_.get_folder_info()->get_device()), file_info{file_info_} {}

auto cluster_file_presence_t::get_file_info() -> model::file_info_t & { return file_info; }
