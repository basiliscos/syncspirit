#pragma once

#include "../device.h"
#include "../file_info.h"
#include "../folder_info.h"
#include "../folder.h"

namespace syncspirit::model {

struct cluster_t;

struct file_interator_t : arc_base_t<file_interator_t> {
    file_interator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_interator_t(const file_interator_t &) = delete;

    inline operator bool() noexcept { return cluster != nullptr; }

    file_info_ptr_t next() noexcept;
    void reset() noexcept;

  private:
    using it_folder_t = typename folders_map_t::iterator_t;
    using it_file_t = typename file_infos_map_t::iterator_t;

    void prepare() noexcept;

    cluster_t *cluster;
    folders_map_t &folders;
    device_ptr_t peer;
    folder_info_ptr_t local_folder_info;
    it_folder_t it_folder;
    it_file_t f_peer_it;
    it_file_t f_peer_end;
    it_file_t f_local_it;
    it_file_t f_local_end;
    file_info_ptr_t file;
};

using file_iterator_ptr_t = intrusive_ptr_t<file_interator_t>;

} // namespace syncspirit::model
