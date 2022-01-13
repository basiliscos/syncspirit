#pragma once

#include "model/cluster.h"
#include "utils/log.h"
#include <rotor.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/filesystem.hpp>
#include <list>
#include <variant>
#include <optional>

namespace syncspirit::fs {

namespace r = rotor;
namespace bfs = boost::filesystem;
namespace sys = boost::system;

namespace payload {

struct scan_folder_t {
    std::string folder_id;
};

};

namespace message {

using scan_folder_t = r::message_t<payload::scan_folder_t>;

}

enum class scan_context_t {
    enter_dir, path_type, modification_times, removing_temporal, file_size
};

struct scan_error_t {
    bfs::path path;
    scan_context_t context;
    sys::error_code ec;
};

struct unchanged_meta_t {
    model::file_info_ptr_t file;
};

struct changed_meta_t {
    model::file_info_ptr_t file;
};

struct incomplete_t {
    model::file_info_ptr_t file;
};

using errors_t = std::vector<scan_error_t>;

using scan_result_t = std::variant<bool, errors_t, changed_meta_t, unchanged_meta_t, incomplete_t>;


struct scan_task_t: boost::intrusive_ref_counter<scan_task_t, boost::thread_unsafe_counter> {
    struct file_info_t {
        model::file_info_ptr_t file;
        bool temp;
    };

    using path_queue_t = std::list<bfs::path>;
    using files_queue_t = std::list<file_info_t>;

    scan_task_t(model::cluster_ptr_t cluster, std::string_view folder_id, const config::fs_config_t& config) noexcept;
    ~scan_task_t();

    scan_result_t advance() noexcept;

  private:
    scan_result_t advance_dir(const bfs::path& dir) noexcept;
    scan_result_t advance_file(const file_info_t& file) noexcept;

    model::cluster_ptr_t cluster;
    model::file_infos_map_t* files;
    utils::logger_t log;
    config::fs_config_t config;

    r::intrusive_ptr_t<message::scan_folder_t> message;
    path_queue_t dirs_queue;
    files_queue_t files_queue;
    bfs::path root;
};

using scan_task_ptr_t = boost::intrusive_ptr<scan_task_t>;

}
