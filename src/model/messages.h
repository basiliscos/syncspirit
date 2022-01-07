#pragma once

#include <rotor/request.hpp>
#include "cluster.h"
#include "diff/block_diff.h"
#include "diff/contact_diff.h"

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
    const void* custom;
};

struct block_update_t {
    model::diff::block_diff_ptr_t diff;
    const void* custom;
};

struct contact_update_t {
    model::diff::contact_diff_ptr_t diff;
    const void* custom;
};

}

namespace message {

using model_update_t = r::message_t<payload::model_update_t>;
using block_update_t = r::message_t<payload::block_update_t>;
using contact_update_t = r::message_t<payload::contact_update_t>;

using model_request_t = r::request_traits_t<payload::model_request_t>::request::message_t;
using model_response_t = r::request_traits_t<payload::model_request_t>::response::message_t;

}

}
