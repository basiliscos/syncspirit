#include "uri.h"
#include <charconv>
#include <regex>

namespace syncspirit::utils {

boost::optional<URI> parse(const char *uri) { return parse(boost::string_view(uri, strlen(uri))); }

boost::optional<URI> parse(const boost::string_view &uri) {
    using result_t = boost::optional<URI>;
    std::regex re("(\\w+)://([^/ :]+):?([^/ ]*)(/?[^ #?]*)\\x3f?([^ #]*)#?([^ ]*)");
    std::cmatch what;
    if (regex_match(uri.begin(), uri.end(), what, re)) {
        auto proto = std::string(what[1].first, what[1].length());
        int port = 0;
        std::from_chars(what[3].first, what[3].second, port);
        if (!port) {
            if (proto == "http") {
                port = 80;
            } else if (proto == "https") {
                port = 443;
            }
        }

        return result_t{URI{
            std::string(uri.begin(), uri.end()), std::string(what[2].first, what[2].length()), // host
            static_cast<std::uint16_t>(port),                                                  // port
            proto,                                                                             // proto
            std::string(what[3].first, what[3].length()),                                      // service
            std::string(what[4].first, what[4].length()),                                      // path
            std::string(what[5].first, what[5].length()),                                      // query
            std::string(what[6].first, what[6].length()),                                      // fragment
        }};
    }
    return result_t{};
}

void URI::reconstruct() noexcept { full = proto + "://" + host + ":" + std::to_string(port) + path + query; }

void URI::set_path(const std::string &value) noexcept {
    path = "";
    if (value.size() && value[0] != '/') {
        path += "/";
    }
    path += value;

    reconstruct();
}

void URI::set_query(const std::string &value) noexcept {
    query = "";
    if (value.size() && value[0] != '?') {
        query += "?";
    }
    query += value;

    reconstruct();
}

std::string URI::relative() const noexcept { return path + query; }

}; // namespace syncspirit::utils
