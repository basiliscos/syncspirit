// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <boost/utility/string_view.hpp>
#include <boost/url.hpp>
#include <boost/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

namespace syncspirit::utils {

struct uri_t;
using uri_ptr_t = boost::intrusive_ptr<uri_t>;

struct SYNCSPIRIT_API uri_t : boost::intrusive_ref_counter<uri_t, boost::thread_unsafe_counter>, boost::urls::url {
    using parent_t = boost::urls::url;
    uri_t(boost::urls::url_view view);

    uri_ptr_t clone() const;
};

using uri_container_t = std::vector<uri_ptr_t>;

SYNCSPIRIT_API uri_ptr_t parse(std::string_view string) noexcept;
SYNCSPIRIT_API bool is_parsable(std::string_view string) noexcept;

} // namespace syncspirit::utils
