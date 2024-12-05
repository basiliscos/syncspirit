// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#include "file_iterator.h"
#include "resolver.h"
#include "model/cluster.h"

#include <spdlog/spdlog.h>

using namespace syncspirit::model;

bool file_iterator_t::file_comparator_t::operator()(const file_info_t *l, const file_info_t *r) const {
    auto le = l->get_blocks().empty();
    auto re = r->get_blocks().empty();

    if (le && !re) {
        return true;
    } else if (re && !le) {
        return false;
    }

    auto ln = l->get_name();
    auto rn = r->get_name();

    return std::lexicographical_compare(ln.begin(), ln.end(), rn.begin(), rn.end());
}

file_iterator_t::file_iterator_t(cluster_t &cluster_, const device_ptr_t &peer_) noexcept
    : cluster{cluster_}, peer{peer_.get()}, folder_index{0} {
    auto &folders = cluster.get_folders();
    comparator.reset(new file_comparator_t());

    for (auto &[folder, _] : folders) {
        auto peer_folder = folder->get_folder_infos().by_device(*peer);
        if (!peer_folder) {
            continue;
        }
        auto my_folder = folder->get_folder_infos().by_device(*cluster.get_device());
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
    auto set = std::make_unique<queue_t>(*comparator);

    for (auto it : files) {
        auto f = it.item.get();
        if (resolve(*f) != advance_action_t::ignore) {
            set->emplace(f);
        }
    }

    auto seen_sequence = peer_folder->get_max_sequence();
    auto it = set->begin();
    folders_list.emplace_back(folder_iterator_t{peer_folder, std::move(set), seen_sequence, it});
    return folders_list.back();
}

auto file_iterator_t::next() noexcept -> result_t {
    auto folders_count = folders_list.size();
    auto folder_scans = size_t{0};

    while (folder_scans < folders_count) {
        auto &fi = folders_list[folder_index];
        auto &file_infos = fi.peer_folder->get_folder()->get_folder_infos();
        auto local_folder = file_infos.by_device(*cluster.get_device());

        auto &queue = fi.files_queue;
        auto folder = local_folder->get_folder();
        auto do_scan = !folder->is_paused() && !folder->is_scheduled() && !queue->empty();

        // check other files
        if (do_scan) {
            auto it = queue->begin();
            while (it != queue->end()) {
                auto &file = **it;
                it = queue->erase(it);
                auto action = resolve(file);
                if (action != advance_action_t::ignore) {
                    return std::make_pair(&file, action);
                }
            }
        }
        folder_index = (folder_index + 1) % folders_count;
        ++folder_scans;
    }
    return {nullptr, advance_action_t::ignore};
}

void file_iterator_t::on_upsert(folder_info_ptr_t peer_folder) noexcept {
    auto folder = peer_folder->get_folder();
    auto &files = peer_folder->get_file_infos();
    for (auto &it : folders_list) {
        if (it.peer_folder->get_folder() == folder) {
            auto &peer_folder = *it.peer_folder;
            auto &files_map = peer_folder.get_file_infos();
            auto seen_sequence = it.seen_sequence;
            auto max_sequence = peer_folder.get_max_sequence();
            auto [from, to] = files_map.range(seen_sequence + 1, max_sequence);
            for (auto fit = from; fit != to; ++fit) {
                auto file = fit->item.get();
                if (resolve(*file) != advance_action_t::ignore) {
                    it.files_queue->insert(file);
                }
            }
            it.seen_sequence = max_sequence;
            it.it = it.files_queue->begin();
            return;
        }
    }
    prepare_folder(peer_folder);
}

void file_iterator_t::on_remove(folder_info_ptr_t peer_folder) noexcept {
    for (auto it = folders_list.begin(); it != folders_list.end(); ++it) {
        if (it->peer_folder == peer_folder) {
            folders_list.erase(it);
            return;
        }
    }
}

void file_iterator_t::recheck(file_info_t &remote) noexcept {
    for (auto &fi : folders_list) {
        if (fi.peer_folder.get() == remote.get_folder_info()) {
            if (resolve(remote) != advance_action_t::ignore) {
                fi.files_queue->emplace(&remote);
            }
            break;
        }
    }
}
