#include "remote_folder_info.h"
#include "folder.h"
#include "cluster.h"

namespace syncspirit::model {

auto remote_folder_info_t::create(std::uint64_t index_id, std::int64_t max_sequence, device_t &device_,
                                  folder_t &folder_) noexcept -> outcome::result<remote_folder_info_t_ptr_t> {
    auto ptr = remote_folder_info_t_ptr_t{new remote_folder_info_t(index_id, max_sequence, device_, folder_)};
    return outcome::success(ptr);
}

remote_folder_info_t::remote_folder_info_t(std::uint64_t index_id_, std::int64_t max_sequence_, device_t &device_,
                                           folder_t &folder_) noexcept
    : index_id{index_id_}, max_sequence{max_sequence_}, device{&device_}, folder{&folder_} {}

std::string_view remote_folder_info_t::get_key() const noexcept { return folder->get_id(); }

remote_folder_info_t_ptr_t remote_folder_infos_map_t::by_folder(const folder_t &folder) const noexcept {
    auto id = folder.get_id();
    auto ptr = (unsigned char *)id.data();
    auto view = utils::bytes_view_t(ptr, id.size());
    return get<0>(view);
}

auto remote_folder_info_t::get_local() const noexcept -> folder_info_ptr_t {
    auto device = folder->get_cluster()->get_device();
    return folder->get_folder_infos().by_device(*device);
}

template <> SYNCSPIRIT_API utils::bytes_view_t get_index<0>(const remote_folder_info_t_ptr_t &item) noexcept {
    auto key = item->get_key();
    auto ptr = (unsigned char *)key.data();
    return {ptr, key.size()};
}

} // namespace syncspirit::model
