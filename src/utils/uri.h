// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include <boost/optional.hpp>
#include <boost/utility/string_view.hpp>
#include <string>
#include <vector>
#include <utility>
#include <cstdint>

namespace syncspirit::utils {

struct SYNCSPIRIT_API URI {
    using StringPair = std::pair<std::string, std::string>;
    using StringPairs = std::vector<StringPair>;

    std::string full;
    std::string host;
    std::uint16_t port;
    std::string proto;
    std::string service;
    std::string path;
    std::string query;
    std::string fragment;

    void set_path(const std::string &value) noexcept;
    void set_query(const std::string &value) noexcept;
    std::string relative() const noexcept;

    StringPairs decompose_query() const noexcept;

    inline bool operator==(const URI &other) const noexcept { return full == other.full; }
    bool operator<(const URI &other) const noexcept;
    explicit inline operator bool() const noexcept { return !full.empty(); }

  private:
    void reconstruct() noexcept;
};

SYNCSPIRIT_API boost::optional<URI> parse(const char *uri);
SYNCSPIRIT_API boost::optional<URI> parse(const boost::string_view &uri);

using uri_container_t = std::vector<URI>;

} // namespace syncspirit::utils
