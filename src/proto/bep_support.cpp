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

outcome::result<message::Hello> parse_hello(asio::mutable_buffer buff) noexcept {
    auto sz = buff.size();
    if (sz < 6)
        return message::Hello();

    std::uint32_t *ptr_32 = reinterpret_cast<std::uint32_t *>(buff.data());
    if (be::big_to_native(*ptr_32++) != constants::bep_magic) {
        return make_error_code(utils::bep_error_code::magic_mismatch);
    }

    std::uint16_t *ptr_16 = reinterpret_cast<std::uint16_t *>(ptr_32);
    std::uint16_t msg_sz = be::big_to_native(*ptr_16++);
    if (msg_sz < sz - 6)
        message::Hello();

    char *ptr = reinterpret_cast<char *>(ptr_16);

    auto msg = std::make_unique<proto::Hello>();
    if (!msg->ParseFromArray(ptr, msg_sz)) {
        return make_error_code(utils::bep_error_code::protobuf_err);
    }
    return message::Hello{std::move(msg), static_cast<size_t>(msg_sz + 6)};
}

} // namespace syncspirit::proto
