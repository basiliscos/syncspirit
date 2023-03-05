#include "remote_folder_info.h"

namespace syncspirit::model {

auto remote_folder_info_t::create(const proto::Device &folder, const device_ptr_t &device_,
                                  const folder_ptr_t &folder_) noexcept -> outcome::result<remote_folder_info_t_ptr_t> {
    auto ptr = remote_folder_info_t_ptr_t{new remote_folder_info_t(folder, device_, folder_)};
    return outcome::success(ptr);
}

remote_folder_info_t::remote_folder_info_t(const proto::Device &folder, const device_ptr_t &device_,
                                           const folder_ptr_t &folder_) noexcept
    : index_id{folder.index_id()}, max_sequence{folder.max_sequence()}, device{device_.get()}, folder{folder_.get()} {}

std::string_view remote_folder_info_t::get_key() const noexcept { return device->device_id().get_sha256(); }

remote_folder_info_t_ptr_t remote_folder_infos_map_t::by_device(const device_ptr_t &device) const noexcept {
    return get<0>(device->device_id().get_sha256());
}

template <> SYNCSPIRIT_API std::string_view get_index<0>(const remote_folder_info_t_ptr_t &item) noexcept {
    return item->get_key();
}

} // namespace syncspirit::model
