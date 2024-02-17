// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "model/device_id.h"
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

template <> struct fmt::formatter<syncspirit::model::device_id_t> {
    using device_id_t = syncspirit::model::device_id_t;

    constexpr auto parse(format_parse_context &ctx) -> format_parse_context::iterator { return ctx.begin(); }

    auto format(const device_id_t &device_id, format_context &ctx) -> format_context::iterator {
        return fmt::format_to(ctx.out(), "{}", device_id.get_short());
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
