// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#pragma once

#include <string>
#include "../../utils/uri.h"

namespace syncspirit::model {

struct discovery_result {
    utils::URI location;
    std::string search_target;
    std::string usn;
};

struct igd_result {
    std::string control_path;
    std::string description_path;
};

} // namespace syncspirit::model
