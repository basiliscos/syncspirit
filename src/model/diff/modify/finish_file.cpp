// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "finish_file.h"

#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

finish_file_t::finish_file_t(const model::file_info_t &file) noexcept {
    assert(file.get_source());
    auto fi = file.get_folder_info();
    auto folder = fi->get_folder();
    folder_id = folder->get_id();
    file_name = file.get_name();
    assert(fi->get_device() == folder->get_cluster()->get_device().get());
}

auto finish_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(*cluster.get_device());
    auto &files = folder_info->get_file_infos();
    auto file = files.by_name(file_name);
    LOG_TRACE(log, "finish_file_t for {}", file->get_full_name());

    auto source = file->get_source();
    assert(source->is_locally_available());

    uuid_t uuid;
    assign(uuid, file->get_uuid());
    auto data = source->as_proto(false);
    auto seq = folder_info->get_max_sequence() + 1;
    data.set_sequence(seq);

    auto opt = file_info_t::create(uuid, data, folder_info);
    if (!opt) {
        return opt.assume_error();
    }
    auto new_file = std::move(opt.assume_value());

    auto &blocks = source->get_blocks();
    for (size_t i = 0; i < blocks.size(); ++i) {
        auto &b = blocks[i];
        assert(b);
        new_file->assign_block(b, i);
        new_file->mark_local_available(i);
    }
    assert(new_file->check_consistency());
    folder_info->add(new_file, true);
    return outcome::success();
}

auto finish_file_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting finish_file_t, folder = {}, file = {}", folder_id, file_name);
    return visitor(*this);
}
