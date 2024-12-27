// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <rotor/request.hpp>
#include <boost/system/errc.hpp>
#include "cluster.h"
#include "diff/cluster_diff.h"

namespace syncspirit::model {

namespace r = rotor;

namespace payload {

struct model_response_t {
    model::cluster_ptr_t cluster;
};

struct model_request_t {
    using response_t = model_response_t;
};

struct model_update_t {
    model::diff::cluster_diff_ptr_t diff;
    const void *custom;
};

} // namespace payload

namespace message {

using model_update_t = r::message_t<payload::model_update_t>;

using model_request_t = r::request_traits_t<payload::model_request_t>::request::message_t;
using model_response_t = r::request_traits_t<payload::model_request_t>::response::message_t;

} // namespace message

} // namespace syncspirit::model
