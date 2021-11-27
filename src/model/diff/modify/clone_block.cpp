#include "clone_block.h"
#include "../block_visitor.h"
#include "../../cluster.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model::diff::modify;

clone_block_t::clone_block_t(const file_info_t& target_file, const block_info_t& block) noexcept {
    target_folder_id = target_file.get_folder_info()->get_folder()->get_id();
    target_file_name = target_file.get_name();

    int found = 0;
    const file_info_t* source_file = nullptr;
    for(auto& b: block.get_file_blocks()) {
        if (b.file() == &target_file && !b.is_locally_available()) {
            target_block_index = b.block_index();
            ++found;
        }
        if (b.is_locally_available()) {
            source_file = b.file();
            source_block_index = b.block_index();
            ++found;
        }
        if (found >= 2) {
            break;
        }
    }
    assert(source_file);
    source_folder_id = source_file->get_folder_info()->get_folder()->get_id();
    source_file_name = source_file->get_name();
}

auto clone_block_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void>  {
    auto target_folder = cluster.get_folders().by_id(target_folder_id);
    auto target_folder_info = target_folder->get_folder_infos().by_device(cluster.get_device());
    auto target_file = target_folder_info->get_file_infos().by_name(target_file_name);
    auto& target_blocks = target_file->get_blocks();
    auto& block = target_blocks[target_block_index];
    block->mark_local_available(target_file.get());
    return outcome::success();
}

auto clone_block_t::visit(block_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting clone_block_t");
    return visitor(*this);
}
