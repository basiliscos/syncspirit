#include "file_iterator.h"
#include "../cluster.h"

using namespace syncspirit::model;

file_interator_t::file_interator_t() noexcept : cluster{nullptr} {}

file_interator_t::file_interator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{&cluster_}, peer{peer_} {
    it_folder = cluster->folders.begin();
    if (it_folder == cluster->folders.end()) {
        cluster = nullptr;
        return;
    }
    prepare();
}

void file_interator_t::prepare() noexcept {
TRY_ANEW:
    if (f_peer_it == f_peer_end) {
        for (;; ++it_folder) {
            if (it_folder == cluster->folders.end()) {
                cluster = nullptr;
                return;
            }
            auto &folder = it_folder->item;
            auto &folder_infos_map = folder->get_folder_infos();

            auto peer_folder_info = folder_infos_map.by_device(peer);
            if (!peer_folder_info)
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
        auto local_file = local_folder_info->get_file_infos().by_name(file->get_name());
        if (!local_file) {
            return;
        }
#if 0
        auto needs_update = !local_file->is_locked() &&
                            (local_file->is_older(*file) ||
                             (local_file->get_sequence() == file->get_sequence() && local_file->is_incomplete()));
#endif
        std::abort();
        bool needs_update = false;
        if (needs_update) {
            return;
        }
    }
    goto TRY_ANEW;
}

void file_interator_t::reset() noexcept { cluster = nullptr; }

file_info_ptr_t file_interator_t::next() noexcept {
    auto r = file_info_ptr_t(file);
    prepare();
    return r;
}
