#include "bep_support.h"
#include "../constants.h"
#include "../utils/error_code.h"
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <algorithm>

using namespace syncspirit;
namespace be = boost::endian;

namespace syncspirit::proto {

void make_hello_message(fmt::memory_buffer &buff, const std::string_view &device_name) noexcept {
    proto::Hello msg;
    msg.set_device_name(device_name.begin(), device_name.size());
    msg.set_client_name(constants::client_name);
    msg.set_client_version(constants::client_version);
    auto sz = static_cast<std::uint16_t>(msg.ByteSizeLong());
    buff.resize(sz + 4 + 2);

    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(buff.begin());
    *ptr_32++ = be::native_to_big(constants::bep_magic);
    std::uint16_t *ptr_16 = reinterpret_cast<std::uint16_t *>(ptr_32);
    *ptr_16++ = be::native_to_big(sz);
    char *ptr = reinterpret_cast<char *>(ptr_16);
    msg.SerializeToArray(ptr, sz);
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

    const char *ptr = reinterpret_cast<const char *>(ptr_16);

    auto msg = std::make_unique<proto::Hello>();
    if (!msg->ParseFromArray(ptr, msg_sz)) {
        return make_error_code(utils::bep_error_code::protobuf_err);
    }
    return wrap(message::Hello{std::move(msg)}, static_cast<size_t>(msg_sz + 6));
}

using MT = proto::MessageType;
template <MT> struct T2M;

template <> struct T2M<MT::CLUSTER_CONFIG> { using type = proto::ClusterConfig; };
template <> struct T2M<MT::INDEX> { using type = proto::Index; };
template <> struct T2M<MT::INDEX_UPDATE> { using type = proto::IndexUpdate; };
template <> struct T2M<MT::REQUEST> { using type = proto::Request; };
template <> struct T2M<MT::RESPONSE> { using type = proto::Response; };
template <> struct T2M<MT::DOWNLOAD_PROGRESS> { using type = proto::DownloadProgress; };
template <> struct T2M<MT::PING> { using type = proto::Ping; };
template <> struct T2M<MT::CLOSE> { using type = proto::Close; };

template <MessageType T>
outcome::result<message::wrapped_message_t> parse(const asio::const_buffer &buff, std::size_t consumed) noexcept {
    using ProtoType = typename T2M<T>::type;
    using MessageType = typename message::as_pointer<ProtoType>;

    const char *ptr = reinterpret_cast<const char *>(buff.data());

    auto msg = std::make_unique<ProtoType>();
    if (!msg->ParseFromArray(ptr, buff.size())) {
        return make_error_code(utils::bep_error_code::protobuf_err);
    }
    return wrap(MessageType{std::move(msg)}, static_cast<size_t>(consumed + buff.size()));
}

outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept {
    auto sz = buff.size();
    if (sz < 4)
        return wrap(message::message_t(), 0u);

    const std::uint32_t *ptr_32 = reinterpret_cast<const std::uint32_t *>(buff.data());
    if (be::big_to_native(*ptr_32) == constants::bep_magic) {
        return parse_hello(asio::buffer(reinterpret_cast<const char *>(buff.data()) + 4, sz - 4));
    } else {
        auto ptr_16 = reinterpret_cast<const std::uint16_t *>(buff.data());
        auto header_sz = be::big_to_native(*ptr_16++);
        if (2 + header_sz > sz)
            return wrap(message::message_t(), 0u);
        proto::Header header;
        if (!header.ParseFromArray(ptr_16, header_sz)) {
            return make_error_code(utils::bep_error_code::protobuf_err);
        }
        auto type = header.type();
        ptr_32 = reinterpret_cast<const std::uint32_t *>(ptr_16 + header_sz);
        auto message_sz = be::big_to_native(*ptr_32++);
        auto tail = static_cast<const std::uint32_t *>(buff.data()) + sz;

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

        if (header.compression() == proto::MessageCompression::NONE) {
            if (ptr_32 + message_sz > tail) {
                return wrap(message::message_t(), 0u);
            }
            auto msg_buff = asio::buffer(reinterpret_cast<const char *>(ptr_32), message_sz);
            return parse_msg(msg_buff, 2 + header_sz + 4);
        } else {
            std::abort();
        }
    }
}

std::size_t make_announce_message(fmt::memory_buffer &buff, const std::string_view &device_name,
                                  const payload::URIs &uris, std::int64_t instance) noexcept {

    assert(buff.size() > 4);
    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(buff.begin());
    *ptr_32++ = be::native_to_big(constants::bep_magic);

    proto::Announce msg;
    msg.set_instance_id(instance);
    for (auto &uri : uris) {
        msg.add_addresses(uri.full);
    }
    msg.set_id(device_name.data(), device_name.size());

    auto sz = msg.ByteSizeLong();
    assert(buff.size() >= sz + 4);
    bool ok = msg.SerializeToArray(ptr_32, sz);
    (void)ok;
    assert(ok);
    return sz + 4;
}

outcome::result<message::Announce> parse_announce(const asio::const_buffer &buff) noexcept {
    auto sz = buff.size();
    if (sz <= 4) {
        return utils::make_error_code(utils::error_code::wrong_magic);
    }

    const std::uint32_t *ptr_32 = reinterpret_cast<const std::uint32_t *>(buff.data());
    if (be::big_to_native(*ptr_32++) != constants::bep_magic) {
        return utils::make_error_code(utils::error_code::wrong_magic);
    }

    auto msg = std::make_unique<proto::Announce>();
    if (!msg->ParseFromArray(ptr_32, sz - 4)) {
        return make_error_code(utils::bep_error_code::protobuf_err);
    }
    return std::move(msg);
}

} // namespace syncspirit::proto
