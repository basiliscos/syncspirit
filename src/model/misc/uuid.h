#pragma once

#include <boost/uuid/uuid.hpp>

namespace syncspirit::model {

static const constexpr size_t uuid_length = 16;
using uuid_t = boost::uuids::uuid;

}
