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

    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(bytes.data());
    *ptr_32++ = be::native_to_big(constants::bep_magic);
    std::uint16_t *ptr_16 = reinterpret_cast<std::uint16_t *>(ptr_32);
    *ptr_16++ = be::native_to_big(bytes.size() - (4 + 6));
    return bytes;
}

// void parse hello

template <typename... Ts> auto wrap(Ts &&...data) noexcept {
    return message::wrapped_message_t{std::forward<Ts>(data)...};
}

static outcome::result<message::wrapped_message_t> parse_hello(const asio::const_buffer &buff) noexcept {
    auto sz = buff.size();
    const std::uint16_t *ptr_16 = reinterpret_cast<const std::uint16_t *>(buff.data());
    std::uint16_t msg_sz = be::big_to_native(*ptr_16++);
    if (msg_sz < sz - 2)
        message::Hello();

    auto ptr = reinterpret_cast<const unsigned char *>(ptr_16);
    auto bytes = utils::bytes_view_t(ptr, msg_sz);

    auto msg = std::make_unique<proto::Hello>();
    if (auto ok = proto::decode(bytes, *msg); !ok) {
        return make_error_code(utils::bep_error_code_t::protobuf_err);
    }
    return wrap(message::Hello{std::move(msg)}, static_cast<size_t>(msg_sz + 6));
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
outcome::result<message::wrapped_message_t> parse(const asio::const_buffer &buff, std::size_t consumed) noexcept {
    using ProtoType = typename T2M<T>::type;
    using MessageType = typename message::as_pointer<ProtoType>;

    auto ptr = reinterpret_cast<const unsigned char *>(buff.data());
    auto bytes = utils::bytes_view_t(ptr, buff.size());

    auto item = ProtoType();
    if (auto ok = proto::decode(bytes, item); !ok) {
        return make_error_code(utils::bep_error_code_t::protobuf_err);
    }
    auto msg = std::make_unique<ProtoType>(std::move(item));
    return wrap(MessageType{std::move(msg)}, static_cast<size_t>(consumed + buff.size()));
}

outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept {
    auto sz = buff.size();
    if (sz < 4)
        return wrap(message::message_t(), 0u);

    const char *ptr = reinterpret_cast<const char *>(buff.data());
    const std::uint32_t *ptr_32 = reinterpret_cast<const std::uint32_t *>(ptr);
    if (be::big_to_native(*ptr_32) == constants::bep_magic) {
        return parse_hello(asio::buffer(ptr + 4, sz - 4));
    } else {
        auto ptr_16 = reinterpret_cast<const std::uint16_t *>(buff.data());
        auto header_sz = be::big_to_native(*ptr_16++);
        if (2 + header_sz > (int)sz) {
            return wrap(message::message_t(), 0u);
        }

        auto header = proto::Header();
        auto header_buff = utils::bytes_view_t((unsigned char*)ptr_16, header_sz);
        if (auto ok = proto::decode(header_buff, header); !ok) {
            return make_error_code(utils::bep_error_code_t::protobuf_err);
        }
        auto type = proto::get_type(header);
        ptr_32 = reinterpret_cast<const std::uint32_t *>(((const char *)ptr_16) + header_sz);
        std::uint32_t message_sz;
        std::memcpy(&message_sz, ptr_32, sizeof(message_sz));
        be::big_to_native_inplace(message_sz);
        ++ptr_32;

        auto tail = ptr + sz;
        if (reinterpret_cast<const char *>(ptr_32) + message_sz > tail) {
            return wrap(message::message_t(), 0u);
        }

        auto parse_msg = [type](const asio::const_buffer &buff, std::size_t consumed) noexcept {
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

        std::vector<char> uncompressed;
        asio::const_buffer msg_buff;
        std::size_t consumed = 2 + header_sz + 4;
        auto compression = proto::get_compression(header);
        if (compression == proto::MessageCompression::NONE) {
            auto msg_buff = asio::buffer(reinterpret_cast<const char *>(ptr_32), message_sz);
            return parse_msg(msg_buff, consumed);
        } else {
            std::uint32_t uncompr_sz;
            std::memcpy(&uncompr_sz, ptr_32, sizeof(uncompr_sz));
            be::big_to_native_inplace(uncompr_sz);
            ++ptr_32;
            auto block_sz = sz - (header_sz + sizeof(std::uint16_t) + sizeof(std::uint32_t) * 2);
            uncompressed.resize(uncompr_sz);
            auto dec =
                LZ4_decompress_safe(reinterpret_cast<const char *>(ptr_32), uncompressed.data(), block_sz, uncompr_sz);
            if (dec < 0) {
                return make_error_code(utils::bep_error_code_t::lz4_decoding);
            }
            msg_buff = asio::buffer(uncompressed.data(), uncompr_sz);
            auto &&r = parse_msg(msg_buff, 0); // consumed is ignored anyway
            if (r) {
                auto &parsed = r.value().message;
                return wrap(std::move(parsed), static_cast<size_t>(consumed + message_sz));
            } else {
                return std::move(r);
            }
        }
    }
}

std::size_t make_announce_message(fmt::memory_buffer &buff, utils::bytes_view_t device_id, const payload::URIs &uris,
                                  std::int64_t instance) noexcept {

    assert(buff.size() > 4);
    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(buff.begin());
    *ptr_32++ = be::native_to_big(constants::bep_magic);

    proto::Announce msg;
    proto::set_id(msg, device_id);
    proto::set_instance_id(msg, instance);
    for (auto &uri : uris) {
        proto::add_addresses(msg, uri->buffer());
    }

    auto bytes = proto::encode(msg);
    auto sz = bytes.size();
    assert(buff.size() >= sz + 4);
    std::copy(bytes.begin(), bytes.end(), (unsigned char*)ptr_32);
    return sz + 4;
}

outcome::result<message::Announce> parse_announce(const asio::const_buffer &buff) noexcept {
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
    return std::make_unique<proto::Announce>(std::move(msg));
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
    proto::encode(header, ptr);
    ptr += header_sz;

    auto message_sz_32 = be::native_to_big(static_cast<std::uint32_t>(header_sz));
    src = reinterpret_cast<const std::uint8_t*>(&message_sz_32);
    *ptr++ = *src++;
    *ptr++ = *src++;
    *ptr++ = *src++;
    *ptr++ = *src++;
    proto::encode(message, ptr);
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
