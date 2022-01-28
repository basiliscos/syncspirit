#pragma once

#include "bep.pb.h"

namespace syncspirit::model {

enum class version_relation_t {
    identity,
    older,
    newer,
    conflict
};

version_relation_t compare(const proto::Vector& lhs, const proto::Vector& rhs) noexcept;

}


