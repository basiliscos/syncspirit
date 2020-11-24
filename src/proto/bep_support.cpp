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
    auto data = msg.SerializeAsString();
    auto sz = static_cast<std::uint16_t>(data.size());
    buff.resize(sz + 4 + 2);

    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(buff.begin());
    *ptr_32++ = be::native_to_big(constants::bep_magic);
    std::uint16_t *ptr_16 = reinterpret_cast<std::uint16_t *>(ptr_32);
    *ptr_16++ = be::native_to_big(sz);
    char *ptr = reinterpret_cast<char *>(ptr_16);
    std::copy(data.begin(), data.end(), ptr);
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

outcome::result<message::wrapped_message_t> parse_bep(const asio::const_buffer &buff) noexcept {
    auto sz = buff.size();
    if (sz < 4)
        return wrap(message::message_t(), 0u);

    const std::uint32_t *ptr_32 = reinterpret_cast<const std::uint32_t *>(buff.data());
    if (be::big_to_native(*ptr_32++) == constants::bep_magic) {
        return parse_hello(asio::buffer(reinterpret_cast<const char *>(buff.data()) + 4, sz - 4));
    }
    std::abort();
}

} // namespace syncspirit::proto
