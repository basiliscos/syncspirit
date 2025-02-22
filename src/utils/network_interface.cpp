// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2022 Ivan Baidakou

#include "network_interface.h"

#if defined(__linux__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <winsock2.h>
#include <iphlpapi.h>
#endif

namespace syncspirit::utils {

#if defined(__linux__) || defined(__APPLE__)
static uri_container_t _local_interfaces(logger_t &log, std::uint16_t port) noexcept {
    uri_container_t r{};

    ifaddrs *ifaddr;
    if (getifaddrs(&ifaddr) == -1) {
        LOG_WARN(log, "getifaddrs failed: ", strerror(errno));
        return r;
    }
    using guard_t = std::unique_ptr<ifaddrs, decltype(&freeifaddrs)>;
    auto guard = guard_t(ifaddr, &freeifaddrs);

    char host[NI_MAXHOST];
    auto lo = inet_addr("127.0.0.1");
    for (auto ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }
        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        auto op = getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
        if (op != 0) {
            LOG_WARN(log, "getnameinfo() failed: {}", gai_strerror(op));
            continue;
        }
        if (((sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == lo) {
            continue;
        }

        auto full = fmt::format("tcp://{}:{}/", host, port);
        auto uri = utils::parse(full);
        r.emplace_back(std::move(uri));
    }

    return r;
}
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
std::string GetLastErrorAsString() {
    // Get the error message ID, if any.
    DWORD errorMessageID = ::GetLastError();
    if (errorMessageID == 0) {
        return std::string(); // No error message has been recorded
    }

    LPSTR messageBuffer = nullptr;

    // Ask Win32 to give us the string version of that message ID.
    // The parameters we pass in, tell Win32 to create the buffer that holds the message for us (because we don't yet
    // know how long the message string will be).
    size_t size =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

    // Copy the error message into a std::string.
    std::string message(messageBuffer, size);

    // Free the Win32's string's buffer.
    LocalFree(messageBuffer);

    return message;
}

static uri_container_t _local_interfaces(logger_t &log, std::uint16_t port) noexcept {
    uri_container_t r{};

    char buff[16 * 1024];
    auto adapter_addresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buff);
    DWORD adapter_addresses_buffer_size = sizeof(buff);
    DWORD code = ::GetAdaptersAddresses(AF_UNSPEC,
                                        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER |
                                            GAA_FLAG_SKIP_FRIENDLY_NAME,
                                        NULL, adapter_addresses, &adapter_addresses_buffer_size);

    if (ERROR_SUCCESS != code) {
        LOG_WARN(log, "GetAdaptersAddresses failed: ", GetLastErrorAsString());
        return r;
    }

    // Iterate through all of the adapters
    for (auto adapter = adapter_addresses; NULL != adapter; adapter = adapter->Next) {
        // Skip loopback adapters
        if (IF_TYPE_SOFTWARE_LOOPBACK == adapter->IfType) {
            continue;
        }

        // Parse all IPv4 and IPv6 addresses
        for (IP_ADAPTER_UNICAST_ADDRESS *address = adapter->FirstUnicastAddress; NULL != address;
             address = address->Next) {
            auto family = address->Address.lpSockaddr->sa_family;
            if (AF_INET == family) {
                // IPv4
                SOCKADDR_IN *ipv4 = reinterpret_cast<SOCKADDR_IN *>(address->Address.lpSockaddr);

                auto host = inet_ntoa(ipv4->sin_addr);
                auto full = fmt::format("tcp://{}:{}/", host, port);
                auto uri = utils::parse(full);
                r.emplace_back(std::move(uri));
            }
        }
    }
    return r;
}
#endif

uri_container_t local_interfaces(const tcp::endpoint &fallback, logger_t &log) noexcept {
    auto port = fallback.port();
    auto r = _local_interfaces(log, port);
    if (r.empty()) {
        auto uri_str = fmt::format("tcp://{0}:{1}/", fallback.address().to_string(), port);
        LOG_DEBUG(log, "falling back into usage {} interface", uri_str);
        auto uri = utils::parse(uri_str);
        r.emplace_back(std::move(uri));
    }
    return r;
}

} // namespace syncspirit::utils
