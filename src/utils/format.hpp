// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#pragma once

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio.hpp>
#include "model/misc/arc.hpp"
#include "syncspirit-export.h"
#include <filesystem>

#include <fmt/format.h>

namespace boost::asio::ip {
class address;
}

namespace syncspirit::model {
struct device_id_t;
struct device_t;
using device_ptr_t = intrusive_ptr_t<device_t>;
struct file_info_t;
using file_info_ptr_t = intrusive_ptr_t<file_info_t>;

} // namespace syncspirit::model

namespace syncspirit::utils {
struct uri_t;
using uri_ptr_t = model::intrusive_ptr_t<uri_t>;
struct bytes_view_t;
struct bytes_t;
} // namespace syncspirit::utils

template <> struct SYNCSPIRIT_API fmt::formatter<std::filesystem::path> {
    using Path = std::filesystem::path;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const Path &path, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<boost::asio::ip::address> {
    using Address = boost::asio::ip::address;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const Address &addr, FormatContext &ctx) const -> decltype(ctx.out());
};

template <typename T> struct SYNCSPIRIT_API fmt::formatter<boost::asio::ip::basic_endpoint<T>> {

    using EndPoint = boost::asio::ip::basic_endpoint<T>;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const EndPoint &p, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::model::device_id_t> {
    using device_id_t = syncspirit::model::device_id_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const device_id_t &device_id, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::model::device_t> {
    using device_t = syncspirit::model::device_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const device_t &device, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::model::device_ptr_t> {
    using device_t = syncspirit::model::device_ptr_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const device_t &device, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::model::file_info_t> {
    using object_t = syncspirit::model::file_info_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const object_t &object, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::model::file_info_ptr_t> {
    using object_t = syncspirit::model::file_info_ptr_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const object_t &object, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::utils::uri_t> {
    using object_t = syncspirit::utils::uri_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const object_t &url, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::utils::uri_ptr_t> {
    using object_t = syncspirit::utils::uri_ptr_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext> auto format(const object_t &url, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::utils::bytes_view_t> {
    using object_t = syncspirit::utils::bytes_view_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const object_t &bytes, FormatContext &ctx) const -> decltype(ctx.out());
};

template <> struct SYNCSPIRIT_API fmt::formatter<syncspirit::utils::bytes_t> {
    using object_t = syncspirit::utils::bytes_t;

    constexpr auto parse(format_parse_context &ctx) -> decltype(ctx.begin()) { return ctx.end(); }

    template <typename FormatContext>
    auto format(const object_t &bytes, FormatContext &ctx) const -> decltype(ctx.out());
};
