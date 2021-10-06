#include "file_iterator.h"
#include "cluster.h"

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
            auto &folder = it_folder->second;
            auto &folder_infos_map = folder->get_folder_infos();

            auto peer_folder_info = folder_infos_map.by_id(peer->get_id());
            if (!peer_folder_info)
                continue;

            auto &peer_file_infos = peer_folder_info->get_file_infos();
            f_peer_it = peer_file_infos.begin();
            f_peer_end = peer_file_infos.end();

            local_folder_info = folder_infos_map.by_id(cluster->get_device()->get_id());

            ++it_folder;
            break;
        }
    }

    while (f_peer_it != f_peer_end) {
        file = f_peer_it->second;
        ++f_peer_it;
        auto full_name = natural_key(file);
        auto local_file = local_folder_info->get_file_infos().by_id(full_name);
        if (!local_file) {
            return;
        }
        auto needs_update = !local_file->is_locked() &&
                            (local_file->is_older(*file) ||
                             (local_file->get_sequence() == file->get_sequence() && local_file->is_dirty()));
        if (needs_update) {
            return;
        }
    }
    goto TRY_ANEW;
}

void file_interator_t::reset() noexcept { cluster = nullptr; }

file_info_ptr_t file_interator_t::next() noexcept {
    file_info_ptr_t r = file->link(cluster->get_device());
    prepare();
    return r;
}
