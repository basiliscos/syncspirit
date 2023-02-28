#pragma once

#include <boost/asio/ip/basic_endpoint.hpp>
#include <boost/filesystem/path.hpp>

#include <fmt/format.h>

template <> struct fmt::formatter<boost::filesystem::path> {
    using Path = boost::filesystem::path;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const Path &path, FormatContext &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", path.string());
    }
};

template <> struct fmt::formatter<boost::asio::ip::address> {
    using Address = boost::asio::ip::address;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const Address &addr, FormatContext &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}", addr.to_string());
    }
};

template <typename T> struct fmt::formatter<boost::asio::ip::basic_endpoint<T>> {

    using EndPoint = boost::asio::ip::basic_endpoint<T>;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const EndPoint &p, FormatContext &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}:{}", p.address(), p.port());
    }
};