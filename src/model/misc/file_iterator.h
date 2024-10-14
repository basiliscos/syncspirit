// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "../device.h"
#include "../file_info.h"
#include "../folder_info.h"
#include "../folder.h"
#include "../diff/cluster_diff.h"
#include "syncspirit-export.h"
#include <vector>
#include <deque>
#include <unordered_set>
#include <unordered_map>

namespace syncspirit::model {

struct diff_sink_t {
    virtual void push(diff::cluster_diff_ptr_t diff) noexcept = 0;
};


struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {
    using files_list_t = std::vector<file_info_ptr_t>;

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    void activate(diff_sink_t& sink) noexcept;
    void deactivate() noexcept;

    file_info_t *next_need_cloning() noexcept;
    file_info_t *next_need_sync() noexcept;
    void commit(file_info_ptr_t file) noexcept;

    void on_clone(file_info_ptr_t file) noexcept;
    void on_upsert(folder_info_ptr_t peer_folder) noexcept;

private:
	struct guard_t : model::arc_base_t<guard_t> {
		guard_t(model::file_info_ptr_t file, file_iterator_t &owner) noexcept;
		~guard_t();

        model::file_info_ptr_t file;
        file_iterator_t &owner;
        bool is_locked;
	};
    using guard_ptr_t = model::intrusive_ptr_t<guard_t>;
    using guarded_files_t = std::unordered_map<std::string_view, guard_ptr_t>;

	struct folder_iterator_t {
		using it_t = typename model::file_infos_map_t::iterator_t;
		model::folder_info_ptr_t peer_folder;
        it_t it_clone;
        it_t it_sync;
        guarded_files_t guarded_files;
	};
	using folder_iterators_t = std::vector<folder_iterator_t>;

    folder_iterator_t& prepare_folder(folder_info_ptr_t peer_folder) noexcept;
    folder_iterator_t& find_folder(folder_t* folder) noexcept;

	cluster_t &cluster;
	device_ptr_t peer;
	std::size_t folder_index;
	diff_sink_t* sink;
	folder_iterators_t folders_list;

	friend struct guard_t;
};

#if 0
struct cluster_t;

struct SYNCSPIRIT_API file_iterator_t : arc_base_t<file_iterator_t> {

    using files_list_t = std::vector<file_info_ptr_t>;
    using files_queue_t = std::deque<file_info_ptr_t>;
    using files_set_t = std::unordered_set<file_info_ptr_t>;

    struct postponed_files_t {
        postponed_files_t(file_iterator_t &);
        ~postponed_files_t();

      private:
        file_iterator_t &iterator;
        files_set_t postponed;
        friend struct file_iterator_t;
    };

    file_iterator_t(cluster_t &cluster, const device_ptr_t &peer) noexcept;
    file_iterator_t(const file_iterator_t &) = delete;

    postponed_files_t prepare_postponed() noexcept;

    file_info_t *current() noexcept;
    void postpone(postponed_files_t &) noexcept;
    void done() noexcept;

    file_info_t *next() noexcept;

    void requeue_unchecked(file_info_ptr_t file) noexcept;
    void append_folder(folder_info_ptr_t peer_folder, files_list_t queue) noexcept;
    void on_upsert(folder_info_ptr_t peer_folder) noexcept;

  private:
    using visited_map_t = std::unordered_map<file_info_ptr_t, std::uint64_t>;
    struct folder_iterator_t {
        model::folder_info_ptr_t peer_folder;
        std::uint64_t index;
        files_list_t files_list;
        std::size_t file_index;
        visited_map_t visited_map;
    };
    using folder_iterators_t = std::vector<folder_iterator_t>;

    void requeue_unchecked(files_set_t set) noexcept;
    bool accept(file_info_t &file, int folder_index, bool check_version = true) noexcept;
    folder_iterator_t prepare_folder(folder_info_ptr_t peer_folder) noexcept;

    file_info_ptr_t next_uncheked() noexcept;
    file_info_ptr_t next_locked() noexcept;
    file_info_ptr_t next_from_folder() noexcept;

    cluster_t &cluster;
    device_ptr_t peer;
    files_queue_t uncheked_list;
    files_queue_t locked_list;
    folder_iterators_t folders_list;
    std::size_t folder_index;
    file_info_ptr_t current_file;

    friend struct postponed_files_t;
};
#endif

using file_iterator_ptr_t = intrusive_ptr_t<file_iterator_t>;

} // namespace syncspirit::model
