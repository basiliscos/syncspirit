#include "share_folder.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"
#include "structs.pb.h"

using namespace syncspirit::model::diff::modify;

auto share_folder_t::apply(cluster_t &cluster) const noexcept -> outcome::result<void> {
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

    auto folder_info = folder->get_folder_info(peer);
    if (folder_info) {
        return make_error_code(error_code_t::folder_is_already_shared);
    }

    auto db = db::FolderInfo();
    db.set_index_id(cluster.next_uint64());
    db.set_max_sequence(0);
    auto fi_opt = folder_info_t::create(cluster.next_uuid(), db, peer, folder);
    if (!fi_opt) {
        return fi_opt.assume_error();
    }

    auto& fi = fi_opt.value();
    folder->add(fi);

    return outcome::success();
}
