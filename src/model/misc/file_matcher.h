// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#pragma once

#include "syncspirit-export.h"
#include "proto/proto-fwd.hpp"

#include <pcre2.h>
#include <string>
#include <string_view>
#include <system_error>

namespace syncspirit::model {

using file_match_t = db::FileMatch;

struct SYNCSPIRIT_API file_matcher_t {
    struct SYNCSPIRIT_API compile_error_t {
        std::error_code code;
        std::size_t error_offset;
    };

    file_matcher_t() noexcept = default;
    file_matcher_t(const file_matcher_t &) noexcept = delete;
    file_matcher_t(file_matcher_t &&) noexcept;
    file_matcher_t(std::string, file_match_t) noexcept;
    ~file_matcher_t();

    file_matcher_t &operator=(file_matcher_t &&) noexcept;

    void set_pattern(std::string_view) noexcept;
    std::string_view get_pattern() const noexcept;
    void set_mode(file_match_t) noexcept;
    file_match_t get_mode() const noexcept;

    compile_error_t compile() noexcept;
    bool is_valid() const noexcept;

    file_match_t match(std::string_view) const noexcept;

  private:
    pcre2_code *re{nullptr};
    pcre2_match_data *match_data{nullptr};
    std::string pattern;
    file_match_t mode{file_match_t::off};
};

} // namespace syncspirit::model
