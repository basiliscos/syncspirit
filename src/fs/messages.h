#pragma once

#include <memory>
#include <unordered_map>
#include <boost/filesystem.hpp>
#include <boost/functional/hash.hpp>
#include <boost/system/error_code.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <rotor.hpp>
#include "../model/block_info.h"
#include "../model/local_file.h"

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

struct write_response_t : r::arc_base_t<write_response_t> {
    opened_file_t file;
    write_response_t(opened_file_t &&f) noexcept : file{std::move(f)} {}
};

struct initial_write_request_t : r::arc_base_t<initial_write_request_t> {
    using response_t = r::intrusive_ptr_t<write_response_t>;
    bfs::path path;
    size_t file_size;
    size_t offset;
    std::string data;
    std::string hash;
    bool final;

    initial_write_request_t(const bfs::path &path_, size_t file_size_, size_t offset_, const std::string &data_,
                            const std::string &hash_, bool final_) noexcept
        : path{path_}, file_size{file_size_}, offset{offset_}, data{data_}, hash{hash_}, final{final_} {}
};

struct write_request_t : r::arc_base_t<write_request_t> {
    using response_t = r::intrusive_ptr_t<write_response_t>;
    bfs::path path;
    opened_file_t file;
    size_t offset;
    std::string data;
    std::string hash;
    bool final;

    write_request_t(const bfs::path &path_, opened_file_t file_, size_t offset_, const std::string &data_,
                    const std::string &hash_, bool final_) noexcept
        : path(path_), file{std::move(file_)}, offset{offset_}, data{data_}, hash{hash_}, final{final_} {}
};

} // namespace payload

namespace message {

using scan_request_t = r::message_t<payload::scan_request_t>;
using scan_response_t = r::message_t<payload::scan_response_t>;
using scan_error_t = r::message_t<payload::scan_error_t>;
using scan_cancel_t = r::message_t<payload::scan_cancel_t>;

using initial_write_request_t = r::request_traits_t<payload::initial_write_request_t>::request::message_t;
using initial_write_response_t = r::request_traits_t<payload::initial_write_request_t>::response::message_t;

using write_request_t = r::request_traits_t<payload::write_request_t>::request::message_t;
using write_response_t = r::request_traits_t<payload::write_request_t>::response::message_t;

} // namespace message

} // namespace fs
} // namespace syncspirit
