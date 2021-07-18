#pragma once

#include <memory>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include <boost/system/error_code.hpp>
#include <rotor.hpp>
#include "../model/block_info.h"
#include "../model/local_file.h"

namespace syncspirit {

namespace fs {

namespace r = rotor;
namespace bfs = boost::filesystem;
namespace sys = boost::system;

namespace payload {

struct scan_request_t {
    bfs::path root;
    r::address_ptr_t reply_to;
    r::request_id_t request_id;
    void *custom_payload;
};

struct scan_response_t {
    model::local_file_map_ptr_t map_info;
    void *custom_payload;
};

struct scan_error_t {
    bfs::path root;
    bfs::path path;
    sys::error_code error;
};

struct scan_cancel_t {
    r::request_id_t request_id;
};

struct write_response_t {};

struct write_request_t {
    using response_t = write_response_t;
    bfs::path path;
    std::string data;
    bool final;
};

} // namespace payload

namespace message {

using scan_request_t = r::message_t<payload::scan_request_t>;
using scan_response_t = r::message_t<payload::scan_response_t>;
using scan_error_t = r::message_t<payload::scan_error_t>;
using scan_cancel_t = r::message_t<payload::scan_cancel_t>;

using write_request_t = r::request_traits_t<payload::write_request_t>::request::message_t;
using write_response_t = r::request_traits_t<payload::write_request_t>::response::message_t;

} // namespace message

} // namespace fs
} // namespace syncspirit
