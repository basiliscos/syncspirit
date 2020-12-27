#pragma once

namespace syncspirit::model {

#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

template <typename T> using arc_base_t = boost::intrusive_ref_counter<T, boost::thread_safe_counter>;

template <typename T> using intrusive_ptr_t = boost::intrusive_ptr<T>;

} // namespace syncspirit::model
