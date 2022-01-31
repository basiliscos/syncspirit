#include "network_interface.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>

namespace syncspirit::utils {

static uri_container_t local_interfaces(logger_t &log, std::uint16_t port) noexcept {
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
        auto uri = utils::parse(full).value();
        r.emplace_back(std::move(uri));
    }

    return r;
}

uri_container_t local_interfaces(const tcp::endpoint &fallback, logger_t &log) noexcept {
    auto port = fallback.port();
    auto r = local_interfaces(log, port);
    if (r.empty()) {
        auto uri_str = fmt::format("tcp://{0}:{1}/", fallback.address().to_string(), port);
        LOG_DEBUG(log, "falling back into usage {} interface", uri_str);
        auto uri = utils::parse(uri_str).value();
        r.emplace_back(std::move(uri));
    }
    return r;
}

} // namespace syncspirit::utils
