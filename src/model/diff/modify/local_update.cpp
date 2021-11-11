#include "local_update.h"
#include "../diff_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

local_update_t::local_update_t(std::string_view folder_id_, std::string_view file_name_, content_change_t current_,
                               std::optional<content_change_t> prev_, bool deleted_, bool invalid_) noexcept:
    folder_id{folder_id_}, file_name{file_name_}, current{current_}, prev{std::move(prev_)}, deleted{deleted_}, invalid{invalid_}
{
}

auto local_update_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    std::abort();
/*
    auto device = cluster.get_device();
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(device);
    auto file = folder_info->get_file_infos().by_name(file_name);

    db::FileInfo fi;
    if (!prev) {
        auto key = file_info_t::create_key(cluster.next_uuid(), folder_info);
        auto file_opt = file_info_t::create(key, fi, folder_info);
        if (!file_opt) {
            return file_opt.assume_error();
        }
        file = std::move(file_opt.value());
    }
    else {
        file->fields_update(fi);
    }
    file->record_update(*device);
    return outcome::success();
*/
}


auto local_update_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting local_update_t");
    //return visitor(*this);
    std::abort();
}
