#pragma once
#include <boost/optional.hpp>
#include <boost/utility/string_view.hpp>
#include <string>

namespace syncspirit::utils {

struct URI {
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

    inline bool operator==(const URI &other) const noexcept { return full == other.full; }

  private:
    void reconstruct() noexcept;
};

boost::optional<URI> parse(const char *uri);
boost::optional<URI> parse(const boost::string_view &uri);

} // namespace syncspirit::utils
