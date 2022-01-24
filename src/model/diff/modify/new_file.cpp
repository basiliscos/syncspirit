#include "new_file.h"
#include "../cluster_visitor.h"
#include "../../cluster.h"

using namespace syncspirit::model::diff::modify;

new_file_t::new_file_t(const model::cluster_t& cluster, std::string_view folder_id_, proto::FileInfo file_, blocks_t blocks_) noexcept:
    folder_id{folder_id_}, file{std::move(file_)}, blocks{std::move(blocks_)}, identical_data{false}, new_uuid{true}
{
    auto &block_map = cluster.get_blocks();
    for(size_t i = 0; i < blocks.size(); ++i) {
        auto& hash = blocks[i].hash();
        auto b = block_map.get(hash);
        if (!b) {
            new_blocks.push_back(i);
        }
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    auto file_infos = folder->get_folder_infos().by_device(cluster.get_device());
    auto prev_file  = file_infos->get_file_infos().by_name(file.name());
    if (!prev_file) {
        return;
    }
    new_uuid = false;

    if (!new_blocks.empty() || blocks.empty()) {
        return;
    }

    auto& prev_blocks = prev_file->get_blocks();
    if (prev_blocks.size() != blocks.size()) {
        return;
    }
    for(size_t i = 0; i < prev_blocks.size(); ++i) {
        auto& bp = prev_blocks[i];
        auto& bn = blocks[i];
        if (bp->get_hash() != bn.hash()) {
            return;
        }
    }
    identical_data = true;
}

auto new_file_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto& blocks_map = cluster.get_blocks();
    for(auto idx:new_blocks) {
        auto block_opt = block_info_t::create(blocks[idx]);
        if (!block_opt) {
            return block_opt.assume_error();
        }
        auto block = std::move(block_opt.assume_value());
        blocks_map.put(block);
    }

    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device(cluster.get_device());
    auto& files = folder_info->get_file_infos();

    auto file = this->file;
    auto seq = folder_info->get_max_sequence() + 1;
    folder_info->set_max_sequence(seq);
    file.set_sequence(seq);

    uuid_t file_uuid;
    if (new_uuid) {
        file_uuid = cluster.next_uuid();
    } else {
        auto prev_file = files.by_name(file.name());
        assign(file_uuid, prev_file->get_uuid());
    }
    auto opt = file_info_t::create(file_uuid, file, folder_info);
    if (!opt) {
        return opt.assume_error();
    }

    auto fi = std::move(opt.value());
    for(size_t i = 0; i < blocks.size(); ++i) {
        auto& b = blocks[i];
        auto block = blocks_map.get(b.hash());
        assert(block);
        fi->assign_block(block, i);
        if (identical_data) {
            fi->mark_local_available(i);
        }
    }

    files.put(fi);
    return outcome::success();
}

auto new_file_t::visit(cluster_visitor_t &visitor) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting new_file_t, folder = {}, file = {}", folder_id, file.name());
    return visitor(*this);
}
