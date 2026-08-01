// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "file_matcher.h"
#include <utility>
#include "utils/error_code.h"

using namespace syncspirit::model;

file_matcher_t::file_matcher_t(std::string pattern_, file_match_t mode_) noexcept
    : pattern{std::move(pattern_)}, mode{mode_} {}

file_matcher_t::file_matcher_t(file_matcher_t &&other) noexcept { *this = std::move(other); }

file_matcher_t::~file_matcher_t() {
    if (match_data) {
        pcre2_match_data_free(match_data);
    }
    if (re) {
        pcre2_code_free(re);
    }
}

file_matcher_t &file_matcher_t::operator=(file_matcher_t &&other) noexcept {
    if (this != &other) {
        re = std::exchange(other.re, nullptr);
        match_data = std::exchange(other.match_data, nullptr);
        pattern = std::exchange(other.pattern, {});
        mode = other.mode;
    }
    return *this;
}

void file_matcher_t::set_pattern(std::string_view value) noexcept { pattern = value; }

std::string_view file_matcher_t::get_pattern() const noexcept { return pattern; }

void file_matcher_t::set_mode(file_match_t value) noexcept { mode = value; }

file_match_t file_matcher_t::get_mode() const noexcept { return mode; }

auto file_matcher_t::compile() noexcept -> compile_error_t {
    auto r = compile_error_t{};
    if (match_data) {
        pcre2_match_data_free(match_data);
        match_data = nullptr;
    }
    if (re) {
        pcre2_code_free(re);
        re = nullptr;
    }
    if (mode != file_match_t::off) {
        int err_num = 0;
        PCRE2_SIZE err_offset = 0;
        re = pcre2_compile((PCRE2_SPTR)pattern.c_str(), pattern.size(), PCRE2_UTF | PCRE2_UCP, &err_num,
                           &r.error_offset, nullptr);
        if (!re) {
            r.code = {err_num, utils::pcre_error_code_category()};
        } else {
            match_data = pcre2_match_data_create_from_pattern(re, nullptr);
        }
    }
    return r;
}

bool file_matcher_t::is_valid() const noexcept { return re && match_data; }

file_match_t file_matcher_t::match(std::string_view file_path) const noexcept {
    auto r = file_match_t::off;
    if (re && match_data && mode != file_match_t::off && pattern.size()) {
        auto code = pcre2_match(re, reinterpret_cast<PCRE2_SPTR>(file_path.data()), file_path.size(), 0, 0, match_data,
                                nullptr);
        if (code >= 0) {
            r = mode;
        }
    }
    return r;
}
