#pragma once
#include <string>
#include <system_error>

namespace syncspirit::utils {

enum class error_code {
    success = 0,
    incomplete_discovery_reply,
    no_location,
    no_st,
    no_usn,
    igd_mismatch,
};

namespace detail {

class error_code_category : public std::error_category {
    virtual const char *name() const noexcept override;
    virtual std::string message(int c) const override;
};

} // namespace detail

const detail::error_code_category &error_code_category();

inline std::error_code make_error_code(error_code e) { return {static_cast<int>(e), error_code_category()}; }

} // namespace syncspirit::utils

namespace std {
template <> struct is_error_code_enum<syncspirit::utils::error_code> : std::true_type {};
} // namespace std
