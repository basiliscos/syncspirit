// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "format.hpp"
#include "model/device.h"
#include "model/device_id.h"
#include "model/file_info.h"
#include "model/folder_info.h"
#include "model/folder.h"
#include "utils/bytes.h"
#include "utils/uri.h"
#include <boost/asio.hpp>
#include <boost/nowide/convert.hpp>

using ec_t = std::error_code;
using sys_ec_t = boost::system::error_code;
using ee_ptr_t = rotor::extended_error_ptr_t;
using ctx_t = fmt::v11::context;
using path_t = std::filesystem::path;
using address_t = boost::asio::ip::address;
using tcp_endpoint_t = boost::asio::ip::tcp::endpoint;
using udp_endpoint_t = boost::asio::ip::udp::endpoint;
using device_id_t = syncspirit::model::device_id_t;
using device_t = syncspirit::model::device_t;
using device_ptr_t = syncspirit::model::intrusive_ptr_t<device_t>;
using file_info_t = syncspirit::model::file_info_t;
using file_info_ptr_t = syncspirit::model::intrusive_ptr_t<file_info_t>;
using uri_t = syncspirit::utils::uri_t;
using uri_ptr_t = syncspirit::model::intrusive_ptr_t<uri_t>;
using bytes_view_t = syncspirit::utils::bytes_view_t;
using bytes_t = syncspirit::utils::bytes_t;

using namespace syncspirit;
using boost::nowide::narrow;

template <typename FormatContext, typename ErrorCode>
auto format_ec(const ErrorCode &ec, FormatContext &ctx) noexcept -> decltype(ctx.out()) {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
    static auto gen_cat = std::string_view(std::generic_category().name());
    static auto sys_cat = std::string_view(std::system_category().name());
    auto cn = ec.category().name();
    auto code = static_cast<DWORD>(ec.value());
    if (code && (cn == gen_cat) || (cn == sys_cat)) {
        static constexpr auto flags =
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
        auto w_msg = (wchar_t *)(nullptr);
        auto sz = FormatMessageW(flags, nullptr, code, 0, (LPWSTR)(&w_msg), 0, nullptr);
        if (sz && w_msg) {
            auto view = std::wstring_view(w_msg, sz);
            auto begin = view.data();
            auto p = begin + (sz - 1);
            auto msg = std::string();
            while (p > begin && (*p == L' ' || *p == L'\n' || *p == L'\t' || *p == L'\r')) {
                --p;
            }
            if (p) {
                auto trimmed_view = std::wstring_view(begin, p - begin);
                msg = narrow(trimmed_view);
            }
            LocalFree(w_msg);
            if (msg.size()) {
                return fmt::format_to(ctx.out(), "{}", msg);
            }
        }
    }
#endif
    return fmt::format_to(ctx.out(), "{}", ec.message());
}

// TODO: remove
template <typename FormatContext>
auto fmt::formatter<path_t>::format(const path_t &path, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", path.string());
}

template <typename FormatContext>
auto fmt::formatter<utils::path_base_t>::format(const utils::path_base_t &path, FormatContext &ctx) const
    -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", path.get_full_name());
}

template <typename FormatContext>
auto fmt::formatter<address_t>::format(const address_t &addr, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", addr.to_string());
}

template <typename FormatContext>
auto fmt::formatter<ee_ptr_t>::format(const ee_ptr_t &ee, FormatContext &ctx) const -> decltype(ctx.out()) {
    auto out = fmt::format_to(ctx.out(), "{} {}", ee->context, ee->ec);
    if (ee->next) {
        return fmt::format_to(out, " <- ", ee->next);
    }
    return out;
}

template <typename FormatContext>
auto fmt::formatter<ec_t>::format(const ec_t &ec, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_ec(ec, ctx);
}

template <typename FormatContext>
auto fmt::formatter<sys_ec_t>::format(const sys_ec_t &ec, FormatContext &ctx) const -> decltype(ctx.out()) {
    return format_ec(ec, ctx);
}

template <typename T>
template <typename FormatContext>
auto fmt::formatter<boost::asio::ip::basic_endpoint<T>>::format(const EndPoint &p, FormatContext &ctx) const
    -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}:{}", p.address(), p.port());
}

template <typename FormatContext>
auto fmt::formatter<device_id_t>::format(const device_id_t &device_id, FormatContext &ctx) const
    -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", device_id.get_short());
}

template <typename FormatContext>
auto fmt::formatter<device_t>::format(const device_t &device, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", device.device_id());
}

template <typename FormatContext>
auto fmt::formatter<device_ptr_t>::format(const device_ptr_t &device, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", *device);
}

template <typename FormatContext>
auto fmt::formatter<file_info_t>::format(const file_info_t &object, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", object.get_name()->get_full_name());
}

template <typename FormatContext>
auto fmt::formatter<file_info_ptr_t>::format(const file_info_ptr_t &object, FormatContext &ctx) const
    -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", *object);
}

template <typename FormatContext>
auto fmt::formatter<uri_t>::format(const uri_t &uri, FormatContext &ctx) const -> decltype(ctx.out()) {
    return fmt::format_to(ctx.out(), "{}", std::string_view(uri.buffer()));
}

template <typename FormatContext>
auto fmt::formatter<uri_ptr_t>::format(const uri_ptr_t &uri, FormatContext &ctx) const -> decltype(ctx.out()) {
    return uri ? fmt::format_to(ctx.out(), "{}", *uri) : fmt::format_to(ctx.out(), "(empty)");
}

template <typename FormatContext>
auto fmt::formatter<bytes_view_t>::format(const bytes_view_t &bytes, FormatContext &ctx) const -> decltype(ctx.out()) {
    auto sz = bytes.size();
    if (sz >= 10) {
        for (size_t i = 0; i < 6; ++i) {
            fmt::format_to(ctx.out(), "{:x}", bytes[i]);
        }
        fmt::format_to(ctx.out(), "..");
        for (size_t i = sz - 2; i < sz; ++i) {
            fmt::format_to(ctx.out(), "{:x}", bytes[i]);
        }
    } else {
        for (auto b : bytes) {
            fmt::format_to(ctx.out(), "{:x}", b);
        }
    }
    return ctx.out();
}

template <typename FormatContext>
auto fmt::formatter<bytes_t>::format(const bytes_t &bytes, FormatContext &ctx) const -> decltype(ctx.out()) {
    auto view = syncspirit::utils::bytes_view_t(bytes);
    return fmt::format_to(ctx.out(), "{}", view);
}

template SYNCSPIRIT_API auto fmt::formatter<std::filesystem::path>::format<ctx_t>(const Path &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<utils::path_base_t>::format<ctx_t>(const utils::path_base_t &,
                                                                               ctx_t &ctx) const -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<address_t>::format<ctx_t>(const address_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<tcp_endpoint_t>::format<ctx_t>(const tcp_endpoint_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<udp_endpoint_t>::format<ctx_t>(const udp_endpoint_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<device_id_t>::format<ctx_t>(const device_id_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<device_t>::format<ctx_t>(const device_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<device_ptr_t>::format<ctx_t>(const device_ptr_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<file_info_t>::format<ctx_t>(const file_info_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<file_info_ptr_t>::format<ctx_t>(const file_info_ptr_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<uri_t>::format<ctx_t>(const uri_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<uri_ptr_t>::format<ctx_t>(const uri_ptr_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<bytes_view_t>::format<ctx_t>(const bytes_view_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<bytes_t>::format<ctx_t>(const bytes_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<ec_t>::format<ctx_t>(const ec_t &, ctx_t &ctx) const -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<sys_ec_t>::format<ctx_t>(const sys_ec_t &, ctx_t &ctx) const
    -> decltype(ctx.out());

template SYNCSPIRIT_API auto fmt::formatter<ee_ptr_t>::format<ctx_t>(const ee_ptr_t &, ctx_t &ctx) const
    -> decltype(ctx.out());
