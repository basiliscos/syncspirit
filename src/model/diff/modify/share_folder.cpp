#include "share_folder.h"
#include "../diff_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "structs.pb.h"

using namespace syncspirit::model::diff::modify;

auto share_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto &folders = cluster.get_folders();
    auto folder = folders.by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::folder_already_exists);
    }

    auto& devices = cluster.get_devices();
    auto peer = devices.by_sha256(peer_id);
    if (!peer) {
        return make_error_code(error_code_t::device_does_not_exist);
    }
    LOG_TRACE(log, "applyging share_folder_t, folder {} with device {}", folder_id, peer->device_id());

    auto folder_info = folder->get_folder_infos().by_device(peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto db = db::FolderInfo();
    db.set_index_id(index);
    auto fi_opt = folder_info_t::create(cluster.next_uuid(), db, peer, folder);
    if (!fi_opt) {
        return fi_opt.assume_error();
    }

    auto& fi = fi_opt.value();
    folder->add(fi);

    return outcome::success();
}

auto share_folder_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting share_folder_t");
    return visitor(*this);
}
