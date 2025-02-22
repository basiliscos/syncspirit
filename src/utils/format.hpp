// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2024 Ivan Baidakou

#pragma once

#include <boost/asio.hpp>
#include "model/device.h"
#include "model/device_id.h"
#include "utils/platform.h"
#include <filesystem>

#include <fmt/format.h>

template <> struct fmt::formatter<std::filesystem::path> {
    using Path = std::filesystem::path;

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

template <> struct fmt::formatter<syncspirit::model::device_id_t> {
    using device_id_t = syncspirit::model::device_id_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const device_id_t &device_id, format_context &ctx) -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", device_id.get_short());
    }
};

template <> struct fmt::formatter<syncspirit::utils::uri_t> {
    using object_t = syncspirit::utils::uri_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const object_t &url, format_context &ctx) -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", std::string_view(url.buffer()));
    }
};

template <> struct fmt::formatter<syncspirit::utils::uri_ptr_t> {
    using object_t = syncspirit::utils::uri_ptr_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const object_t &url, format_context &ctx) const -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", *url);
    }
};

template <> struct fmt::formatter<syncspirit::model::device_t> {
    using device_t = syncspirit::model::device_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const device_t &device, format_context &ctx) -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", device.device_id());
    }
};

template <> struct fmt::formatter<syncspirit::model::device_ptr_t> {
    using device_t = syncspirit::model::device_ptr_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const device_t &device, format_context &ctx) -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", *device);
    }
};
