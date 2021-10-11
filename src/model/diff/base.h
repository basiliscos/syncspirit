#pragma once

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::model::diff {

struct base_t : boost::intrusive_ref_counter<base_t, boost::thread_safe_counter> {
    virtual ~base_t();
};

} // namespace syncspirit::model::diff
