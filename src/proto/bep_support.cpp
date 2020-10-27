#include "bep_support.h"
#include "hello.pb.h"
#include "../constants.h"
#include <boost/endian/arithmetic.hpp>
#include <boost/endian/conversion.hpp>
#include <algorithm>

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

} // namespace syncspirit::proto
