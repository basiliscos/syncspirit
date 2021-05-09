#pragma once

#include <vector>
#include <memory>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include <boost/system/error_code.hpp>
#include <rotor.hpp>
#include "../model/block_info.h"

namespace std {
template <> struct hash<boost::filesystem::path> {
    size_t operator()(const boost::filesystem::path &p) const { return boost::filesystem::hash_value(p); }
};
} // namespace std

namespace syncspirit {

namespace fs {

namespace r = rotor;
namespace bfs = boost::filesystem;
namespace sys = boost::system;

namespace payload {

struct scan_request_t {
    bfs::path root;
    r::address_ptr_t reply_to;
};

struct scan_response_t {
    using blocks_t = std::vector<model::block_info_ptr_t>;
    using file_map_t = std::unordered_map<bfs::path, blocks_t>;
    bfs::path root;
    file_map_t file_map;
};

struct scan_error_t {
    bfs::path root;
    bfs::path path;
    sys::error_code error;
};


} // namespace payload

namespace message {

using scan_request_t = r::message_t<payload::scan_request_t>;
using scan_response_t = r::message_t<payload::scan_response_t>;
using scan_error_t = r::message_t<payload::scan_error_t>;

} // namespace message

} // namespace fs
} // namespace syncspirit
