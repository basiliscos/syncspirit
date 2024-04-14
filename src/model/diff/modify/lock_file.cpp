// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "lock_file.h"
#include "db/prefix.h"
#include "utils/format.hpp"
#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

lock_file_t::lock_file_t(const model::file_info_t &file, bool locked_) noexcept
    : folder_id{file.get_folder_info()->get_folder()->get_id()},
      device_id(file.get_folder_info()->get_device()->device_id().get_sha256()), file_name{file.get_name()},
      locked{locked_} {}

auto lock_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto d = cluster.get_devices().by_sha256(this->device_id);
    auto folder_info = folder->get_folder_infos().by_device(*d);
    auto file = folder_info->get_file_infos().by_name(file_name);

    LOG_TRACE(log, "applyging lock_file_t for '{}' ({}); lock = {}", file->get_full_name(), d, locked);
    if (locked) {
        assert(!file->is_locked());
        file->lock();
    } else {
        assert(file->is_locked());
        file->unlock();
    }

    return outcome::success();
}

auto lock_file_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting lock_file_t");
    return visitor(*this, custom);
}
