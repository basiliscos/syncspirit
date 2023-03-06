#include "remote_folder_info.h"
#include "folder.h"
#include "cluster.h"

namespace syncspirit::model {

auto remote_folder_info_t::create(const proto::Device &folder, const device_ptr_t &device_,
                                  const folder_ptr_t &folder_) noexcept -> outcome::result<remote_folder_info_t_ptr_t> {
    auto ptr = remote_folder_info_t_ptr_t{new remote_folder_info_t(folder, device_, folder_)};
    return outcome::success(ptr);
}

remote_folder_info_t::remote_folder_info_t(const proto::Device &folder, const device_ptr_t &device_,
                                           const folder_ptr_t &folder_) noexcept
    : index_id{folder.index_id()}, max_sequence{folder.max_sequence()}, device{device_.get()}, folder{folder_.get()} {}

std::string_view remote_folder_info_t::get_key() const noexcept { return folder->get_id(); }

bool remote_folder_info_t::needs_update() const noexcept {
    auto local = get_local();
    return (local->get_index() != index_id) || local->get_max_sequence() > max_sequence;
}

remote_folder_info_t_ptr_t remote_folder_infos_map_t::by_folder(const folder_ptr_t &folder) const noexcept {
    return get<0>(folder->get_id());
}

auto remote_folder_info_t::get_local() const noexcept -> folder_info_ptr_t {
    auto device = folder->get_cluster()->get_device();
    return folder->get_folder_infos().by_device(device);
}

template <> SYNCSPIRIT_API std::string_view get_index<0>(const remote_folder_info_t_ptr_t &item) noexcept {
    return item->get_key();
}

} // namespace syncspirit::model
