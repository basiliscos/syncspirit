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

struct diget_request_t {
    using response_t = digest_response_t;
    std::string_view data;
    const void *custom;
};

struct validation_response_t {
    bool valid;
};

struct validation_request_t : r::arc_base_t<validation_request_t> {
    using response_t = validation_response_t;
    std::string data;
    std::string hash;
    const void *custom;

    validation_request_t(const std::string_view data_, const std::string &hash_, const void *custom_ = nullptr) noexcept
        : data{data_}, hash{hash_}, custom{custom_} {}
};

} // namespace payload

namespace message {

using diget_request_t = r::request_traits_t<payload::diget_request_t>::request::message_t;
using digest_response_t = r::request_traits_t<payload::diget_request_t>::response::message_t;

using validation_request_t = r::request_traits_t<payload::validation_request_t>::request::message_t;
using validation_response_t = r::request_traits_t<payload::validation_request_t>::response::message_t;

} // namespace message

} // namespace hasher
} // namespace syncspirit
