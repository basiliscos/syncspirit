#pragma once

#include <rotor/actor_base.h>
#include "model/folder.h"

namespace syncspirit::test {
namespace {
namespace to {
struct device {};
struct state {};
}
}
}


namespace syncspirit::model {

template <> inline auto &folder_t::access<test::to::device>() noexcept { return device; }

}

namespace rotor {

template <> inline auto &actor_base_t::access<syncspirit::test::to::state>() noexcept { return state; }

}
