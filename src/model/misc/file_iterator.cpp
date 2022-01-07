#include "file_iterator.h"
#include "../cluster.h"

using namespace syncspirit::model;

file_interator_t::file_interator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{&cluster_}, peer{peer_}, folders{cluster->get_folders()} {
    it_folder = folders.begin();
    if (it_folder == folders.end()) {
        cluster = nullptr;
        return;
    }
    f_peer_it = f_peer_end = it_file_t{};
    prepare();
}

void file_interator_t::prepare() noexcept {
TRY_ANEW:
    if (f_peer_it == f_peer_end) {
        for (;; ++it_folder) {
            if (it_folder == folders.end()) {
                cluster = nullptr;
                return;
            }
            auto &folder = it_folder->item;
            auto &folder_infos_map = folder->get_folder_infos();

            auto peer_folder_info = folder_infos_map.by_device(peer);
            if (!peer_folder_info || !peer_folder_info->is_actual())
                continue;

            auto &peer_file_infos = peer_folder_info->get_file_infos();
            f_peer_it = peer_file_infos.begin();
            f_peer_end = peer_file_infos.end();

            local_folder_info = folder_infos_map.by_device(cluster->get_device());

            ++it_folder;
            break;
        }
    }

    while (f_peer_it != f_peer_end) {
        file = f_peer_it->item;
        ++f_peer_it;
        if (file->is_locally_locked()) {
            continue;
        }
        auto local_file = local_folder_info->get_file_infos().by_name(file->get_name());
        if (!local_file) {
            return;
        }
        bool needs_download = local_file->need_download(*file);
        if (needs_download) {
            return;
        }
    }
    goto TRY_ANEW;
}

void file_interator_t::reset() noexcept {
    cluster = nullptr;
#if 0
    auto folders = cluster->folders;
    f_local_it = f_local_end = it_file_t{};
#endif
}

file_info_ptr_t file_interator_t::next() noexcept {
    auto r = file_info_ptr_t(file);
    prepare();
    return r;
}
