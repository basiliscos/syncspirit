#include "append_block.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

append_block_t::append_block_t(const file_info_t& file, size_t block_index_, std::string data_) noexcept:
    block_index{block_index_}, data{std::move(data_)} {
    folder_id = file.get_folder_info()->get_folder()->get_id();
    file_name = file.get_name();
}

auto append_block_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void>  {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(cluster.get_device());
    auto file = folder_info->get_file_infos().by_name(file_name);
    file->mark_local_available(block_index);
    return outcome::success();
}

auto append_block_t::visit(block_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting append_block_t");
    return visitor(*this);
}
