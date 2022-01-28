#include "append_block.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

append_block_t::append_block_t(const file_info_t &file, size_t block_index_, std::string data_) noexcept
    : block_diff_t{file, block_index_}, data{std::move(data_)} {}

auto append_block_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(device_id);
    auto file = folder_info->get_file_infos().by_name(file_name);
    LOG_TRACE(log, "append_block_t append_block_t, appending block {} to {}", block_index, file->get_full_name());
    file->mark_local_available(block_index);
    return outcome::success();
}

auto append_block_t::visit(block_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting append_block_t");
    return visitor(*this);
}
