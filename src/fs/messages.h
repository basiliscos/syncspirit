#pragma once

#include <memory>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include <boost/system/error_code.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <rotor.hpp>
#include "../model/block_info.h"
#include "../model/misc/local_file.h"

namespace syncspirit {

namespace fs {

namespace r = rotor;
namespace bfs = boost::filesystem;
namespace bio = boost::iostreams;
namespace sys = boost::system;

using opened_file_t = std::unique_ptr<bio::mapped_file>;

namespace payload {

struct scan_request_t {
    bfs::path root;
    r::address_ptr_t reply_to;
    r::request_id_t request_id;
};

struct scan_response_t {
    model::local_file_map_ptr_t map_info;
};

struct scan_error_t {
    bfs::path root;
    bfs::path path;
    sys::error_code error;
};

struct scan_cancel_t {
    r::request_id_t request_id;
};

struct open_response_t : r::arc_base_t<open_response_t> {
    opened_file_t file;
    open_response_t(opened_file_t &&f) noexcept : file{std::move(f)} {}
};

struct open_request_t {
    using response_t = r::intrusive_ptr_t<open_response_t>;
    bfs::path path;
    size_t file_size; /* 0 = read only */
    const void *custom;
};

struct close_response_t {};

struct close_request_t : r::arc_base_t<close_request_t> {
    using response_t = close_response_t;
    opened_file_t file;
    bfs::path path;
    bool complete;
    close_request_t(opened_file_t &&f, const bfs::path &path_, bool complete_ = true) noexcept
        : file{std::move(f)}, path{path_}, complete{complete_} {}
};

struct clone_request_t {
    using response_t = r::intrusive_ptr_t<open_response_t>;
    bfs::path source;
    bfs::path target;
    size_t target_size;
    size_t block_size;
    size_t source_offset;
    size_t target_offset;
    opened_file_t target_file; // optional
};

} // namespace payload

namespace message {

using scan_request_t = r::message_t<payload::scan_request_t>;
using scan_response_t = r::message_t<payload::scan_response_t>;
using scan_error_t = r::message_t<payload::scan_error_t>;
using scan_cancel_t = r::message_t<payload::scan_cancel_t>;

using open_request_t = r::request_traits_t<payload::open_request_t>::request::message_t;
using open_response_t = r::request_traits_t<payload::open_request_t>::response::message_t;

using close_request_t = r::request_traits_t<payload::close_request_t>::request::message_t;
using close_response_t = r::request_traits_t<payload::close_request_t>::response::message_t;

using clone_request_t = r::request_traits_t<payload::clone_request_t>::request::message_t;
using clone_response_t = r::request_traits_t<payload::clone_request_t>::response::message_t;

} // namespace message

} // namespace fs
} // namespace syncspirit
