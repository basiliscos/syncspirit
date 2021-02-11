#pragma once

#include "model/folder.h"

namespace syncspirit::test {
namespace {
namespace to {
struct device {};
struct devices {};
}
}
}


namespace syncspirit::model {

template <> inline auto &folder_t::access<test::to::device>() noexcept { return device; }
template <> inline auto &folder_t::access<test::to::devices>() noexcept { return devices; }

}
