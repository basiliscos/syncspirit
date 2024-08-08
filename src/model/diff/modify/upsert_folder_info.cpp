#include "upsert_folder_info.h"

#include "model/cluster.h"
#include "model/diff/cluster_visitor.h"
#include "model/misc/error_code.h"

using namespace syncspirit::model::diff::modify;

upsert_folder_info_t::upsert_folder_info_t(const uuid_t &uuid_, std::string_view device_id_,
                                           std::string_view folder_id_, std::uint64_t index_id_,
                                           std::int64_t max_sequence_) noexcept
    : device_id{device_id_}, folder_id{folder_id_}, index_id{index_id_}, max_sequence{max_sequence_} {}

auto upsert_folder_info_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "applying upsert_folder_info_t, folder_id: {}", folder_id);
    auto device = cluster.get_devices().by_sha256(device_id);
    if (!device) {
        return make_error_code(error_code_t::no_such_device);
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    if (!folder) {
        return make_error_code(error_code_t::no_such_folder);
    }

    db::FolderInfo db;
    db.set_index_id(index_id);
    db.set_max_sequence(max_sequence);

    auto opt = folder_info_t::create(uuid, db, device, folder);
    if (!opt) {
        return opt.assume_error();
    }

    auto &fi = opt.value();
    folder->get_folder_infos().put(fi);

    return applicator_t::apply_sibling(cluster);
}

auto upsert_folder_info_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting upsert_folder_info_t");
    return visitor(*this, custom);
}
