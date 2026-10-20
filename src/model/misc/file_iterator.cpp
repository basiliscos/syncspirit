// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "file_iterator.h"
#include "resolver.h"
#include "model/cluster.h"
#include "model/misc/folder_info_iterator.h"

using namespace syncspirit::model;

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_.get()}, folder_index{0} {

    for (auto& it: cluster.get_folders()) {
        check(it.item->is_shared_with(*peer));
    }
};

file_iterator_t::~file_iterator_t() {}

void file_iterator_t::check(model::folder_info_t* peer_folder) noexcept {
    if (peer_folder) {
        auto can_receive = peer_folder->get_folder()->get_folder_type() != db::FolderType::send;
        if (can_receive) {
            auto it = model::folder_info_iterator_ptr_t(new model::folder_info_iterator_t(peer_folder));
            auto ss = substate_t{peer_folder, std::move(it), peer_folder->get_index(), 0};
            substates.push_back(std::move(ss));
        }
    }
}

void file_iterator_t::on_upsert(folder_t &folder) noexcept {
    check(folder.is_shared_with(*peer));
}

auto file_iterator_t::next() noexcept -> result_t {
    auto folders_count = substates.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &ss = substates[folder_index];
        auto& [peer_folder, it, seen_index, seen_sequence] = ss;
        auto folder = peer_folder->get_folder();
        auto &local_folder = *folder->get_folder_info(*cluster.get_device());
        auto do_scan = !folder->is_paused() && !folder->is_scheduled() && !folder->is_suspended() && !it->is_finished();
        auto local_device = cluster.get_device().get();

        if (do_scan) {
            if (auto view = peer->get_remote_view_map().get(peer->device_id().get_sha256(), folder->get_id()); view) {
                do_scan = view->max_sequence <= peer_folder->get_max_sequence();
            } else {
                do_scan = false;
            }
        }
        // check other files
        if (do_scan) {
            for (auto file = it->next(); file; file = it->next()) {
                if (!cluster.is_locked(file->get_name().get())) {
                    auto local_file = static_cast<model::file_info_t*>(file->get_entity()->get_presence(local_device));
                    auto action = resolve(*file, local_file, local_folder);
                    if (action != advance_action_t::ignore) {
                        return {file, peer_folder.get(), local_file, action};
                    }
                }

            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {nullptr, nullptr, nullptr, advance_action_t::ignore};
}

void file_iterator_t::on_upsert(folder_info_ptr_t peer_folder) noexcept {
    auto folder = peer_folder->get_folder();
    auto can_receive = peer_folder->get_folder()->get_folder_type() != db::FolderType::send;
    if (can_receive) {
        for (auto& ss: substates) {
            if (ss.fi->get_folder() == folder) {
                auto peer_index = peer_folder->get_index();
                if (peer_index != ss.seen_index) {
                    ss.seen_index = peer_index;
                    ss.it->reset(true);
                } else {
                    ss.it->reset(false);
                }
                return;
            }
        }
        auto it = model::folder_info_iterator_ptr_t(new model::folder_info_iterator_t(peer_folder));
        auto ss = substate_t{peer_folder, std::move(it), peer_folder->get_index(), 0};
        substates.push_back(std::move(ss));
    }
}

#if 0
#include <spdlog/spdlog.h>

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_.get()}, folder_index{0} {
    auto &folders = cluster.get_folders();

    for (auto &[folder, _] : folders) {
        auto peer_folder = folder->is_shared_with(*peer);
        if (!peer_folder) {
            continue;
        }
        auto my_folder = folder->is_shared_with(*cluster.get_device());
        if (!my_folder) {
            continue;
        }
        prepare_folder(std::move(peer_folder));
    }
}

auto file_iterator_t::find_folder(folder_t *folder) noexcept -> folder_iterator_t & {
    auto predicate = [&](auto &it) -> bool { return it.peer_folder->get_folder() == folder; };
    auto it = std::find_if(folders_list.begin(), folders_list.end(), predicate);
    return *it;
}

auto file_iterator_t::prepare_folder(folder_info_ptr_t peer_folder) noexcept -> folder_iterator_t & {
    auto &files = peer_folder->get_file_infos();
    auto folder = peer_folder->get_folder();
    auto &local_folder = *folder->get_folder_info(*folder->get_cluster()->get_device());
    auto &local_files = local_folder.get_file_infos();
    auto order = folder->get_pull_order();
    auto set = std::make_unique<queue_t>(file_comparator_t{order});
    auto seen_index = std::uint64_t{0};
    auto seen_sequence = std::int64_t{0};
    bool can_receive = folder->get_folder_type() != db::FolderType::send;

    if (can_receive) {
        for (auto &it : files) {
            auto f = it.get();
            auto local_file = local_files.by_name(f->get_name()->get_full_name());
            if (resolve(*f, local_file.get(), local_folder) != advance_action_t::ignore) {
                set->emplace(f);
            }
        }
        seen_index = peer_folder->get_index();
        seen_sequence = peer_folder->get_max_sequence();
    }

    auto it = set->begin();
    folders_list.emplace_back(
        folder_iterator_t{peer_folder, std::move(set), seen_index, seen_sequence, it, can_receive});
    return folders_list.back();
}

void file_iterator_t::on_upsert(folder_info_ptr_t peer_folder) noexcept {
    auto folder = peer_folder->get_folder();
    for (auto &it : folders_list) {
        if (it.peer_folder->get_folder() == folder && it.can_receive) {
            return populate(it);
        }
    }
    prepare_folder(peer_folder);
}

void file_iterator_t::populate(folder_iterator_t &it) noexcept {
    auto peer_folder = it.peer_folder.get();
    auto seen_sequence = it.seen_sequence;
    if (peer_folder->get_index() != it.seen_index) {
        it.seen_sequence = 0;
        it.files_queue->clear();
    }
    auto &files_map = peer_folder->get_file_infos();
    auto folder = peer_folder->get_folder();
    auto &local_folder = *folder->get_folder_info(*folder->get_cluster()->get_device());
    auto &local_files = local_folder.get_file_infos();
    auto max_sequence = peer_folder->get_max_sequence();
    auto [from, to] = files_map.range(seen_sequence + 1, max_sequence);
    for (auto fit = from; fit != to; ++fit) {
        auto file = fit->get();
        auto local_file = local_files.by_name(file->get_name()->get_full_name());
        if (resolve(*file, local_file.get(), local_folder) != advance_action_t::ignore) {
            it.files_queue->insert(file);
        }
    }
    it.seen_sequence = max_sequence;
    it.it = it.files_queue->begin();
}

void file_iterator_t::on_upsert(folder_t &folder) noexcept {
    auto order = folder.get_pull_order();
    for (auto &it : folders_list) {
        if (it.peer_folder->get_folder() == &folder) {
            auto &peer_folder = *it.peer_folder;
            auto folder_type = peer_folder.get_folder()->get_folder_type();
            auto can_receive = folder_type != db::FolderType::send;
            if (can_receive) {
                if (!it.can_receive) {
                    it.seen_sequence = 0;
                    populate(it);
                } else if (it.files_queue->key_comp().pull_order != order) {
                    auto new_set = std::make_unique<queue_t>(file_comparator_t{order});
                    for (auto fi : *it.files_queue) {
                        new_set->insert(fi);
                    }
                    it.files_queue = std::move(new_set);
                    it.it = it.files_queue->begin();
                }
            } else {
                if (it.can_receive) {
                    it.files_queue->clear();
                    it.it = it.files_queue->begin();
                }
            }
            it.can_receive = can_receive;
        }
    }
}

void file_iterator_t::on_remove(folder_info_ptr_t peer_folder) noexcept {
    for (auto it = folders_list.begin(); it != folders_list.end(); ++it) {
        if (it->peer_folder == peer_folder) {
            folders_list.erase(it);
            folder_index = 0;
            return;
        }
    }
}

void file_iterator_t::recheck(const folder_info_t &remote_fi, file_info_t &remote) noexcept {
    for (auto &fi : folders_list) {
        if (fi.peer_folder.get() == &remote_fi) {
            if (fi.can_receive) {
                auto folder = fi.peer_folder->get_folder();
                auto &local_folder = *folder->get_folder_info(*folder->get_cluster()->get_device());
                auto &local_files = local_folder.get_file_infos();
                auto local_file = local_files.by_name(remote.get_name()->get_full_name());
                if (resolve(remote, local_file.get(), local_folder) != advance_action_t::ignore) {
                    fi.files_queue->emplace(&remote);
                }
            }
            break;
        }
    }
}
#endif