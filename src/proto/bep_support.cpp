// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "bep_support.h"
#include "constants.h"
#include "utils/error_code.h"
#include "proto/proto-helpers.h"
#include "syncspirit-config.h"
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <algorithm>
#include <lz4.h>
#include <spdlog/spdlog.h>

using namespace syncspirit;
namespace be = boost::endian;

namespace syncspirit::proto {

utils::bytes_t make_hello_message(std::string_view device_name) noexcept {
    proto::Hello msg;
    proto::set_device_name(msg, device_name);
    proto::set_client_name(msg, constants::client_name);
    proto::set_client_version(msg, SYNCSPIRIT_VERSION);
    auto bytes = proto::encode(msg, 4 + 2);

    auto magic = be::native_to_big(constants::bep_magic);
    auto dst = (unsigned char*)bytes.data();
    auto src = (unsigned char*)&magic;
    *dst++ = *src++;
    *dst++ = *src++;
    *dst++ = *src++;
    *dst++ = *src++;

    // auto sz = be::native_to_big(uint16_t(bytes.size() - (4 + 6)));
    auto sz = be::native_to_big(uint16_t(bytes.size() - 6));
    src = (unsigned char*)&sz;
    *dst++ = *src++;
    *dst++ = *src++;

    return bytes;
}

// void parse hello

template <typename... Ts> auto wrap(Ts &&...data) noexcept {
    return message::wrapped_message_t{std::forward<Ts>(data)...};
}

static outcome::result<message::wrapped_message_t> parse_hello(utils::bytes_view_t buff) noexcept {
    auto sz = buff.size();
    const std::uint16_t *ptr_16 = reinterpret_cast<const std::uint16_t *>(buff.data());
    std::uint16_t msg_sz = be::big_to_native(*ptr_16++);
    if (msg_sz < sz - 2) {
        return wrap(Hello(), static_cast<size_t>(0));
    }

    auto ptr = reinterpret_cast<const unsigned char *>(ptr_16);
    auto bytes = utils::bytes_view_t(ptr, msg_sz);

    auto msg = proto::Hello();
    if (auto left = proto::decode(bytes, msg); left != 0) {
        return make_error_code(utils::bep_error_code_t::protobuf_err);
    }
    return wrap(std::move(msg), static_cast<size_t>(msg_sz + 6));
}

using MT = proto::MessageType;
template <MT> struct T2M;

template <> struct T2M<MT::CLUSTER_CONFIG> {
    using type = proto::ClusterConfig;
};
template <> struct T2M<MT::INDEX> {
    using type = proto::Index;
};
template <> struct T2M<MT::INDEX_UPDATE> {
    using type = proto::IndexUpdate;
};
template <> struct T2M<MT::REQUEST> {
    using type = proto::Request;
};
template <> struct T2M<MT::RESPONSE> {
    using type = proto::Response;
};
template <> struct T2M<MT::DOWNLOAD_PROGRESS> {
    using type = proto::DownloadProgress;
};
template <> struct T2M<MT::PING> {
    using type = proto::Ping;
};
template <> struct T2M<MT::CLOSE> {
    using type = proto::Close;
};

template <typename M> struct M2T;
template <> struct M2T<typename proto::ClusterConfig> {
    using type = std::integral_constant<MT, MT::CLUSTER_CONFIG>;
};
template <> struct M2T<typename proto::Index> {
    using type = std::integral_constant<MT, MT::INDEX>;
};
template <> struct M2T<typename proto::IndexUpdate> {
    using type = std::integral_constant<MT, MT::INDEX_UPDATE>;
};
template <> struct M2T<typename proto::Request> {
    using type = std::integral_constant<MT, MT::REQUEST>;
};
template <> struct M2T<typename proto::Response> {
    using type = std::integral_constant<MT, MT::RESPONSE>;
};
template <> struct M2T<typename proto::DownloadProgress> {
    using type = std::integral_constant<MT, MT::DOWNLOAD_PROGRESS>;
};
template <> struct M2T<typename proto::Ping> {
    using type = std::integral_constant<MT, MT::PING>;
};
template <> struct M2T<typename proto::Close> {
    using type = std::integral_constant<MT, MT::CLOSE>;
};

template <MessageType T>
outcome::result<message::wrapped_message_t> parse(utils::bytes_view_t buff, std::size_t consumed) noexcept {
    using ProtoType = typename T2M<T>::type;

    auto item = ProtoType();
    if (auto left = proto::decode(buff, item); left) {
        return make_error_code(utils::bep_error_code_t::protobuf_err);
    }
    return wrap(std::move(item), static_cast<size_t>(consumed + buff.size()));
}

static auto bep_magic_big = be::native_to_big(constants::bep_magic);
static auto bep_magic_bytes = utils::bytes_view_t((unsigned char*)&bep_magic_big, sizeof(bep_magic_big));

outcome::result<message::wrapped_message_t> parse_bep(utils::bytes_view_t buff) noexcept {
    auto sz = buff.size();
    if (sz < 4) {
        return wrap(message::message_t(), 0u);
    }

    auto head = buff.subspan(0, 4);
    if (head == bep_magic_bytes) {
        return parse_hello(buff.subspan(4));
    } else {
        auto ptr = head.data();
        auto header_sz = std::uint16_t();
        auto dst = (unsigned char*)(&header_sz);
        *dst++ = *ptr++;
        *dst++ = *ptr++;
        be::big_to_native_inplace(header_sz);
        if (2 + header_sz > (int)sz) {
            return wrap(message::message_t(), 0u);
        }

        auto header = proto::Header();
        auto header_buff = head.subspan(2, header_sz);
        if (auto left = proto::decode(header_buff, header); left) {
            return make_error_code(utils::bep_error_code_t::protobuf_err);
        }
        auto type = proto::get_type(header);
        ptr += header_sz;
        std::uint32_t message_sz;
        dst = (unsigned char*)(&message_sz);
        *dst++ = *ptr++;
        *dst++ = *ptr++;
        *dst++ = *ptr++;
        *dst++ = *ptr++;
        be::big_to_native_inplace(message_sz);

        // auto tail = ptr + sz;
        if (ptr + message_sz > buff.data() + sz) {
            return wrap(message::message_t(), 0u);
        }

        auto parse_msg = [type](utils::bytes_view_t buff, std::size_t consumed) noexcept {
            switch (type) {
            case MT::CLUSTER_CONFIG:
                return parse<MT::CLUSTER_CONFIG>(buff, consumed);
            case MT::INDEX:
                return parse<MT::INDEX>(buff, consumed);
            case MT::INDEX_UPDATE:
                return parse<MT::INDEX_UPDATE>(buff, consumed);
            case MT::REQUEST:
                return parse<MT::REQUEST>(buff, consumed);
            case MT::RESPONSE:
                return parse<MT::RESPONSE>(buff, consumed);
            case MT::DOWNLOAD_PROGRESS:
                return parse<MT::DOWNLOAD_PROGRESS>(buff, consumed);
            case MT::PING:
                return parse<MT::PING>(buff, consumed);
            case MT::CLOSE:
                return parse<MT::CLOSE>(buff, consumed);
            default:
                std::abort();
            }
        };

        auto rest = utils::bytes_view_t(ptr, message_sz);
        std::size_t consumed = 2 + header_sz + 4;
        auto compression = proto::get_compression(header);
        if (compression == proto::MessageCompression::NONE) {
            return parse_msg(rest, consumed);
        } else {
            std::uint32_t uncompr_sz;
            dst = (unsigned char*)(&uncompr_sz);
            *dst++ = *ptr++;
            *dst++ = *ptr++;
            *dst++ = *ptr++;
            *dst++ = *ptr++;
            be::big_to_native_inplace(uncompr_sz);

            auto msg_buff = utils::bytes_view_t();

            auto block_sz = sz - (header_sz + sizeof(std::uint16_t) + sizeof(std::uint32_t) * 2);
            std::vector<char> uncompressed;
            uncompressed.resize(uncompr_sz);
            auto data_ptr = reinterpret_cast<const char *>(ptr);
            auto dec = LZ4_decompress_safe(data_ptr, uncompressed.data(), block_sz, uncompr_sz);
            if (dec < 0) {
                return make_error_code(utils::bep_error_code_t::lz4_decoding);
            }
            auto msg_bytes = utils::bytes_view_t((unsigned char*)uncompressed.data(), uncompr_sz);
            auto &&r = parse_msg(msg_bytes, 0); // consumed is ignored anyway
            if (r) {
                auto &parsed = r.value().message;
                return wrap(std::move(parsed), static_cast<size_t>(consumed + message_sz));
            } else {
                return std::move(r);
            }
        }
    }
}

std::size_t make_announce_message(utils::bytes_view_t buff, utils::bytes_view_t device_id, const payload::URIs &uris,
                                  std::int64_t instance) noexcept {


    proto::Announce msg;
    proto::set_id(msg, device_id);
    proto::set_instance_id(msg, instance);
    for (auto &uri : uris) {
        proto::add_addresses(msg, uri->buffer());
    }

    auto msg_sz = proto::estimate(msg) ;
    auto total_sz = msg_sz + 4;
    assert(buff.size() >= msg_sz);

    auto magic = be::native_to_big(constants::bep_magic);
    auto dst = const_cast<unsigned char*>(buff.data());
    auto src = reinterpret_cast<unsigned char*>(&magic);
    *dst++ = *src++;
    *dst++ = *src++;
    *dst++ = *src++;
    *dst++ = *src++;

    proto::encode(msg, utils::bytes_view_t(dst, msg_sz));
    return total_sz;
}

outcome::result<Announce> parse_announce(utils::bytes_view_t buff) noexcept {
    auto sz = buff.size();
    if (sz <= 4) {
        return utils::make_error_code(utils::error_code_t::wrong_magic);
    }

    const std::uint32_t *ptr_32 = reinterpret_cast<const std::uint32_t *>(buff.data());
    if (be::big_to_native(*ptr_32++) != constants::bep_magic) {
        return utils::make_error_code(utils::error_code_t::wrong_magic);
    }

    auto ptr = (unsigned char*)(ptr_32);
    proto::Announce msg;
    if (auto ok = proto::decode({ptr, sz - 4}, msg); ok) {
        return make_error_code(utils::bep_error_code_t::protobuf_err);
    }
    return msg;
}

template <typename Message>
SYNCSPIRIT_API utils::bytes_t serialize(const Message &message,
                                        proto::MessageCompression compression) noexcept {
    using type = typename M2T<Message>::type;
    auto message_sz = proto::estimate(message);
    proto::Header header(type::value, compression);
    auto header_sz = proto::estimate(header);
    auto header_sz_16 = be::native_to_big(static_cast<std::uint16_t>(header_sz));
    auto src = reinterpret_cast<const std::uint8_t*>(&header_sz_16);
    auto bytes = utils::bytes_t(2 + header_sz + 4 + message_sz);
    auto *ptr = reinterpret_cast<std::uint8_t *>(bytes.data());
    *ptr++ = *src++;
    *ptr++ = *src++;
    proto::encode(header, {ptr, header_sz});
    ptr += header_sz;

    auto message_sz_32 = be::native_to_big(static_cast<std::uint32_t>(message_sz));
    src = reinterpret_cast<const std::uint8_t*>(&message_sz_32);
    *ptr++ = *src++;
    *ptr++ = *src++;
    *ptr++ = *src++;
    *ptr++ = *src++;
    proto::encode(message, {ptr, message_sz});
    return bytes;
}

template utils::bytes_t SYNCSPIRIT_API serialize(const proto::ClusterConfig &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::Index &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::IndexUpdate &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::Request &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::Response &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::DownloadProgress &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::Ping &message,
                                       proto::MessageCompression compression) noexcept;
template utils::bytes_t SYNCSPIRIT_API serialize(const proto::Close &message,
                                       proto::MessageCompression compression) noexcept;

} // namespace syncspirit::proto
