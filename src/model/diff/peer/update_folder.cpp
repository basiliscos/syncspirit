#include "update_folder.h"
#include "model/diff/diff_visitor.h"
#include "../../misc/error_code.h"

using namespace syncspirit::model;
using namespace syncspirit::model::diff::peer;

update_folder_t::update_folder_t(std::string_view folder_id_, std::string_view peer_id_, files_t files_) noexcept:
    folder_id{std::string(folder_id_)}, peer_id{std::string(peer_id_)}, files{std::move(files_)} {}

auto update_folder_t::apply_impl(cluster_t &cluster) const noexcept -> outcome::result<void> {
    auto folder = cluster.get_folders().by_id(folder_id);
    auto folder_info = folder->get_folder_infos().by_device_id(peer_id);
    auto& files_map = folder_info->get_file_infos();
    auto& blocks_map = cluster.get_blocks();

    for(const auto& f: files) {
        auto opt = file_info_t::create(cluster.next_uuid(), f, folder_info);
        if (!opt) {
            return opt.assume_error();
        }
        auto& file = opt.value();
        files_map.put(file);

        for(int i = 0; i < f.block_size(); ++i) {
            auto& b = f.blocks(i);
            auto block = blocks_map.get(b.hash());
            if (!block) {
                auto opt = block_info_t::create(b);
                if (!opt) {
                    return opt.assume_error();
                }
                blocks_map.put(block);
            }
            file->add_block(block);
        }
    }
    return outcome::success();
}

auto update_folder_t::visit(diff_visitor_t &visitor) const noexcept -> outcome::result<void> {
    return visitor(*this);
}

using diff_t = diff::cluster_diff_ptr_t;

template<typename T>
static auto instantiate(const cluster_t &cluster, const device_t& source, const T& message) noexcept -> outcome::result<diff_t> {
    auto folder = cluster.get_folders().by_id(message.folder());
    if (folder) {
        return make_error_code(error_code_t::folder_does_not_exist);
    }

    auto device_id = source.device_id().get_sha256();
    auto fi = folder->get_folder_infos().by_device_id(device_id);
    if (!fi) {
        return make_error_code(error_code_t::folder_is_not_shared);
    }

    update_folder_t::files_t files;
    files.resize(static_cast<size_t>(message.files_size()));
    for(int i = 0; i < message.files_size(); ++i) {
        files.emplace_back(std::move(message.files(i)));
    }

    auto diff = diff_t(new update_folder_t(message.folder(), device_id, std::move(files)));
    return outcome::success(std::move(diff));
}

auto update_folder_t::create(const cluster_t &cluster, const model::device_t& source, const proto::Index& message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, source, message);
}

auto update_folder_t::create(const cluster_t &cluster, const model::device_t& source, const proto::IndexUpdate& message) noexcept -> outcome::result<cluster_diff_ptr_t> {
    return instantiate(cluster, source, message);
}
