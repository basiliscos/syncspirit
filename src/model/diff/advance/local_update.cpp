// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "local_update.h"
#include "model/cluster.h"
#include "../cluster_visitor.h"
#include "model/diff/apply_controller.h"
#include "proto/proto-helpers-bep.h"
#include <memory_resource>

using namespace syncspirit::model::diff::advance;

local_update_t::local_update_t(const cluster_t &cluster, sequencer_t &sequencer, proto::FileInfo proto_file_,
                               std::string_view folder_id_) noexcept
    : advance_t(folder_id_, cluster.get_device()->device_id().get_sha256(), advance_action_t::local_update) {

    auto buffer = std::array<std::byte, 256>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<std::string>(&pool);

    auto &device = *cluster.get_devices().by_sha256(peer_id);
    auto folder = cluster.get_folders().by_id(folder_id);
    auto &self = *cluster.get_device();
    auto self_id = self.device_id().get_sha256();

    auto &folder_infos = folder->get_folder_infos();
    auto local_folder_info = folder_infos.by_device_id(self_id);
    auto &local_files = local_folder_info->get_file_infos();
    auto name = std::pmr::string(proto::get_name(proto_file_), allocator);
    auto local_file = local_files.by_name(name);

    if (auto original_file = get_original(folder_infos, device, proto_file_); original_file) {
        initialize(cluster, sequencer, original_file->as_proto(true), name);
    } else {
        proto::set_modified_by(proto_file_, self.device_id().get_uint());
        initialize(cluster, sequencer, std::move(proto_file_), name);
        auto version = version_t();
        if (local_file) {
            version = local_file->get_version();
            version.update(device);
        } else {
            version = version_t(device);
        }
        auto &proto_version = proto::get_version(proto_local);
        version.to_proto(proto_version);
    }
}

auto local_update_t::get_original(const model::folder_infos_map_t &fis, const model::device_t &self,
                                  const proto::FileInfo &local_file) const noexcept -> model::file_info_ptr_t {
    auto r = model::file_info_ptr_t();
    auto name = proto::get_name(local_file);
    auto local_deleted = proto::get_deleted(local_file);
    auto local_invalid = proto::get_invalid(local_file);
    auto local_perms = proto::get_permissions(local_file);
    auto local_no_perms = proto::get_no_permissions(local_file);
    auto local_size = proto::get_size(local_file);
    auto local_type = model::file_info_t::as_flags(proto::get_type(local_file));

    for (auto &it_fi : fis) {
        auto fi = it_fi.item.get();
        auto peer_file = model::file_info_ptr_t();
        if (fi->get_device() != &self) {
            auto &peer_files = fi->get_file_infos();
            auto candidate = peer_files.by_name(name);
            if (candidate) {
                if (candidate->get_size() == local_size) {
                    bool matches = local_type == candidate->get_type() && local_deleted == candidate->is_deleted() &&
                                   local_invalid == candidate->is_invalid() &&
                                   (local_no_perms == candidate->has_no_permissions() ||
                                    local_perms == candidate->get_permissions());

                    if (matches) {
                        if (candidate->is_file()) {
                            matches = false;
                            auto local_blocks_sz = proto::get_blocks_size(local_file);
                            auto iterator = candidate->iterate_blocks();
                            if (iterator.get_total() == static_cast<std::uint32_t>(local_blocks_sz)) {
                                matches = true;
                                auto i = std::uint32_t{0};
                                while (auto pb = iterator.next()) {
                                    auto &lb = proto::get_blocks(local_file, i);
                                    if (pb->get_hash() != proto::get_hash(lb)) {
                                        matches = false;
                                        break;
                                    }
                                    ++i;
                                }
                            }
                        }
                        if (matches) {
                            peer_file = std::move(candidate);
                        }
                    }
                }
            }
        }
        if (peer_file) {
            if (!r) {
                r = std::move(peer_file);
            } else {
                if (model::compare(*peer_file, *r) > 1) {
                    r = std::move(peer_file);
                }
            }
        }
    }
    return r;
}

auto local_update_t::apply_impl(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    auto &cluster = controller.get_cluster();
    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is not available, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else if (folder->is_suspended()) {
        LOG_DEBUG(log, "remote_copy_t, folder = {}, name = {}, folder is suspended, ignoring", folder_id,
                  proto::get_name(proto_source));
    } else {
        return advance_t::apply_impl(controller, custom);
    }
    return applicator_t::apply_sibling(controller, custom);
}

auto local_update_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t, folder = {}, file = {}", folder_id, proto::get_name(proto_local));
    return visitor(*this, custom);
}
