#include "uuid.h"
#include <cassert>

namespace syncspirit::model {

void assign(uuid_t& uuid, std::string_view source) noexcept {
    assert(source.size() == uuid.size());
    auto data = (const uint8_t*)source.data();
    std::copy(data, data + source.size(), uuid.data);
}

}
