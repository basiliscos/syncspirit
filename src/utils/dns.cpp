// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "dns.h"

#include <cctype>
#include <boost/asio/ip/address.hpp>

namespace syncspirit::utils {

using size_t = typename std::string::size_type;
using tcp = boost::asio::ip::tcp;
using udp = boost::asio::ip::udp;

std::vector<endpoint_t> parse_dns_servers(std::string_view str) noexcept {
    std::vector<endpoint_t> r;
    auto begin = size_t{0};
    auto end = str.find(',', begin);
    while (begin != std::string::npos) {
        auto full_addr = str.substr(begin, end - begin);
        begin = (end == std::string::npos) ? std::string::npos : end + 1;
        if (end == std::string::npos) {
            begin = std::string::npos;
        } else {
            begin = end + 1;
            end = str.find(',', begin);
        }
        auto colon_start = full_addr.rfind(':');
        size_t addr_start = 0;
        size_t addr_end = colon_start;
        if (full_addr.size() && full_addr[0] == '[') {
            if (auto pos = full_addr.rfind(']'); pos != std::string::npos) {
                addr_start = 1;
                addr_end = pos - 1;
            }
        }

        auto ip_str = full_addr.substr(addr_start, addr_end);
        auto ec = boost::system::error_code{};
        auto ip_addr = boost::asio::ip::make_address(ip_str, ec);
        if (ec) {
            continue;
        }
        std::uint16_t port = 53;
        if (colon_start != std::string::npos) {
            auto port_start = colon_start + 1;
            auto port_end = port_start;
            while (port_end < full_addr.size() && std::isdigit(full_addr[port_end])) {
                ++port_end;
            }
            auto port_str = full_addr.substr(port_start, port_end - port_start);
            uint16_t p;
            auto result = std::from_chars(port_str.begin(), port_str.end(), p);
            if (result.ec == std::errc()) {
                port = p;
            }
        }
        r.emplace_back(endpoint_t{ip_addr, port});
    }
    return r;
}

template <typename Protocol, typename Endpoint>
auto make_endpoints(const std::vector<ip::address> &addresses, std::uint16_t port) noexcept -> addresses_t<Endpoint> {
    using R = std::vector<Endpoint>;
    R r;
    r.reserve(addresses.size());
    for (auto &ip : addresses) {
        r.emplace_back(ip, port);
    }
    return std::make_shared<R>(std::move(r));
}

template auto make_endpoints<tcp, typename tcp::endpoint>(const std::vector<ip::address> &addresses,
                                                          std::uint16_t port) noexcept -> addresses_t<tcp::endpoint>;

} // namespace syncspirit::utils
