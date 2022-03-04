#pragma once

#include <string>
#include <string_view>
#include <rotor.hpp>

namespace syncspirit {
namespace hasher {

namespace r = rotor;

namespace payload {

struct digest_response_t {
    std::string digest;
    uint32_t weak;
};

struct digest_request_t {
    using response_t = digest_response_t;
    std::string data;
    size_t block_index;
    r::message_ptr_t custom;
};

struct validation_response_t {
    bool valid;
};

struct validation_request_t : r::arc_base_t<validation_request_t> {
    using response_t = validation_response_t;
    std::string_view data;
    std::string hash;
    r::message_ptr_t custom;

    validation_request_t(std::string_view data_, std::string hash_, r::message_ptr_t custom_ = nullptr) noexcept
        : data{data_}, hash{hash_}, custom{std::move(custom_)} {}
};

} // namespace payload

namespace message {

using digest_request_t = r::request_traits_t<payload::digest_request_t>::request::message_t;
using digest_response_t = r::request_traits_t<payload::digest_request_t>::response::message_t;

using validation_request_t = r::request_traits_t<payload::validation_request_t>::request::message_t;
using validation_response_t = r::request_traits_t<payload::validation_request_t>::response::message_t;

} // namespace message

} // namespace hasher
} // namespace syncspirit
