#pragma once
#include "generic_diff.hpp"

namespace syncspirit::model::diff {

namespace modify {
    struct append_block_t;
    struct clone_block_t;
}


template<>
struct generic_visitor_t<tag::block> {
    virtual ~generic_visitor_t() = default;

    virtual outcome::result<void> operator()(const modify::append_block_t &) noexcept;
    virtual outcome::result<void> operator()(const modify::clone_block_t &) noexcept;
};

using block_visitor_t = generic_visitor_t<tag::block>;

}
