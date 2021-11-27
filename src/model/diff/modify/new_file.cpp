#include "new_file.h"
#include "db/prefix.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

new_file_t::new_file_t(std::string_view folder_id_, proto::FileInfo file_, blocks_t blocks_) noexcept:
    folder_id{folder_id_}, file{std::move(file_)}, blocks{std::move(blocks_)}
{
}

auto new_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(cluster.get_device());

    auto file = this->file;
    auto seq = folder_info->get_max_sequence() + 1;
    folder_info->set_max_sequence(seq);
    file.set_sequence(seq);

    auto opt = file_info_t::create(cluster.next_uuid(), file, folder_info);
    if (!opt) {
        return opt.assume_error();
    }

    auto fi = std::move(opt.value());

    auto& blocks_map = cluster.get_blocks();
    for(size_t i = 0; i < blocks.size(); ++i) {
        auto& b = blocks[i];
        auto block = blocks_map.get(b.hash());
        if (!block) {
            auto block_opt = block_info_t::create(b);
            if (!block_opt) {
                return block_opt.assume_error();
            }
            block = std::move(block_opt.assume_value());
            blocks_map.put(block);
        }
        fi->assign_block(block, i);
    }

    folder_info->get_file_infos().put(fi);

    return outcome::success();
}

auto new_file_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting new_file_t");
    return visitor(*this);
}
