// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "proto-fwd.hpp"
#include "proto-impl.h"
#include "syncspirit-export.h"
#include "utils/bytes.h"
#include <cstdint>

namespace syncspirit::proto {

using bytes_view_t = utils::bytes_view_t;

namespace details {

// clang-format off

using Announce = pp::message<
    pp::bytes_field     <"_id",          1              >,
    pp::string_field    <"_addresses",   1, pp::repeated>,
    pp::uint64_field    <"_instance_id", 3              >
>;

using Hello = pp::message<
    pp::string_field    <"_device_name",    1>,
    pp::string_field    <"_client_name",    2>,
    pp::string_field    <"_client_version", 3>
>;

using Header = pp::message<
    pp::enum_field <"_type",        1, MessageType>,
    pp::enum_field <"_compression", 2, MessageCompression>
>;

using Device = pp::message<
    pp::bytes_field     <"_id",                         1              >,
    pp::string_field    <"_name",                       2              >,
    pp::string_field    <"_addresses",                  3, pp::repeated>,
    pp::enum_field      <"_compression",                4, Compression >,
    pp::string_field    <"_cert_name",                  5              >,
    pp::int64_field     <"_max_sequence",               6              >,
    pp::bool_field      <"_introducer",                 7              >,
    pp::uint64_field    <"_index_id",                   8              >,
    pp::bool_field      <"_skip_introduction_removals", 9              >
>;

using Folder = pp::message<
    pp::string_field    <"_id",                    1                      >,
    pp::string_field    <"_label",                 2                      >,
    pp::bool_field      <"_read_only",             3                      >,
    pp::bool_field      <"_ignore_permissions",    4                      >,
    pp::bool_field      <"_ignore_delete",         5                      >,
    pp::bool_field      <"_disable_temp_indexes",  6                      >,
    pp::bool_field      <"_paused",                7                      >,
    pp::message_field   <"_devices",              16, Device, pp::repeated>
>;

using ClusterConfig = pp::message<
    pp::message_field   <"_folders", 1, Folder, pp::repeated>
>;

using Counter = pp::message<
    pp::uint64_field    <"_id",    1>,
    pp::uint64_field    <"_value", 2>
>;

using Vector = pp::message<
    pp::message_field   <"_counters", 1, Counter, pp::repeated>
>;

using BlockInfo = pp::message<
    pp::int64_field    <"_offset",    1>,
    pp::int32_field    <"_size",      2>,
    pp::bytes_field    <"_hash",      3>,
    pp::uint32_field   <"_weak_hash", 4>
>;

using FileInfo = pp::message<
    pp::string_field    <"_name",            1                           >,
    pp::enum_field      <"_type",            2, FileInfoType             >,
    pp::int64_field     <"_size",            3                           >,
    pp::uint32_field    <"_permissions",     4                           >,
    pp::int64_field     <"_modified_s",      5                           >,
    pp::int32_field     <"_modified_ns",     11                          >,
    pp::uint64_field    <"_modified_by",     12                          >,
    pp::bool_field      <"_deleted",         6                           >,
    pp::bool_field      <"_invalid",         7                           >,
    pp::bool_field      <"_no_permissions",  8                           >,
    pp::message_field   <"_version",         9, Vector                   >,
    pp::int64_field     <"_sequence",        10                          >,
    pp::int32_field     <"_block_size",      13                          >,
    pp::message_field   <"_blocks",          16, BlockInfo, pp::repeated >,
    pp::string_field    <"_symlink_target",  17                          >
>;

using Index = pp::message<
    pp::string_field    <"_folder",  1                        >,
    pp::message_field   <"_files",   2, FileInfo, pp::repeated>
>;

using IndexUpdate = Index;

using Request = pp::message<
    pp::int32_field     <"_id",              1>,
    pp::string_field    <"_folder",          2>,
    pp::string_field    <"_name",            3>,
    pp::int64_field     <"_offset",          4>,
    pp::int32_field     <"_size",            5>,
    pp::bytes_field     <"_hash",            6>,
    pp::bool_field      <"_from_temporary",  7>,
    pp::uint32_field    <"_weak_hash",       8>
>;

using Response = pp::message<
    pp::int32_field     <"_id",      1>,
    pp::bytes_field     <"_data",    2>,
    pp::enum_field      <"_code",    3, ErrorCode>
>;

using FileDownloadProgressUpdate = pp::message<
    pp::enum_field      <"_update_type",     1, FileDownloadProgressUpdateType>,
    pp::string_field    <"_name",            2                                >,
    pp::message_field   <"_version",         3, Vector                        >,
    pp::int32_field     <"_block_indexes",   4, pp::repeated                  >
>;

using DownloadProgress = pp::message<
    pp::string_field    <"_folder",                     1                                          >,
    pp::message_field   <"_FileDownloadProgressUpdate", 2, FileDownloadProgressUpdate, pp::repeated>
>;

// using Ping = pp::message<>;
struct Ping {};

using Close = pp::message<
    pp::string_field <"_reason", 1>
>;

// clang-format on

};

namespace changeable {

struct SYNCSPIRIT_API Announce {
    Announce(details::Announce* impl_= nullptr): impl{impl_} {}
    inline void id(utils::bytes_view_t value) noexcept {
        using namespace pp;
        auto ptr = (std::byte*)(value.data());
        auto bytes = utils::bytes_t(value.begin(), value.end());
        (*impl)["_id"_f] = std::move(bytes);
    }
    inline void instance_id(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_instance_id"_f] = value;
    }
    void add_addresses(std::string value) noexcept;
    details::Announce* impl;
};

struct SYNCSPIRIT_API Hello {
    Hello(details::Hello* impl_= nullptr): impl{impl_} {}
    inline void device_name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_device_name"_f] = std::string(value);
    }
    inline void client_name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_client_name"_f] = std::string(value);
    }
    inline void client_version(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_client_version"_f] = std::string(value);
    }
    details::Hello* impl;
};

struct SYNCSPIRIT_API Header {
    Header(details::Header* impl_= nullptr): impl{impl_} {}
    inline void type(MessageType value) noexcept {
        using namespace pp;
        (*impl)["_type"_f] = value;
    }
    inline void compression(MessageCompression value) noexcept {
        using namespace pp;
        (*impl)["_compression"_f] = value;
    }
    details::Header* impl;
};

struct SYNCSPIRIT_API BlockInfo: proto::impl::changeable::BlockInfo<details::BlockInfo> {
    using parent_t = proto::impl::changeable::BlockInfo<details::BlockInfo>;
    using parent_t::parent_t;

    inline void offset(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_offset"_f] = value;
    }

    inline void hash(utils::bytes_view_t value) noexcept {
        using namespace pp;
        auto ptr = (std::byte*)(value.data());
        auto bytes = utils::bytes_t(value.begin(), value.end());
        (*impl)["_hash"_f] = std::move(bytes);
    }
};

struct SYNCSPIRIT_API Counter {
    Counter(details::Counter* impl_= nullptr): impl{impl_} {}
    inline void id(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_id"_f] = value;
    }
    inline void value(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_value"_f] = value;
    }
    details::Counter* impl;
};

struct SYNCSPIRIT_API Vector {
    Vector(details::Vector* impl_): impl{impl_}{ assert(impl); };
    Vector(const Vector&) = delete;
    Vector(Vector&&) = delete;

    inline details::Vector& expose() noexcept { return *impl; }
    inline void clear_counters() noexcept {
        using namespace pp;
        (*impl)["_counters"_f].clear();
    }
    void add_counter(proto::Counter value) noexcept;
    Counter add_new_counter() noexcept;

    private:
    details::Vector* impl;
};

struct SYNCSPIRIT_API FileInfo: proto::impl::changeable::FileInfo<details::FileInfo> {
    using parent_t = proto::impl::changeable::FileInfo<details::FileInfo>;
    using parent_t::parent_t;

    inline void clear_blocks() noexcept {
        using namespace pp;
        (*impl)["_blocks"_f].clear();
    }
    Vector mutable_version() noexcept;
    void add_block(proto::BlockInfo value) noexcept;
};

struct SYNCSPIRIT_API Device: proto::impl::changeable::Device<details::Device> {
    using parent_t = proto::impl::changeable::Device<details::Device>;
    using parent_t::parent_t;

    inline void id(utils::bytes_view_t value) noexcept {
        using namespace pp;
        auto ptr = (std::byte*)(value.data());
        auto bytes = utils::bytes_t(value.begin(), value.end());
        (*impl)["_id"_f] = std::move(bytes);
    }
    inline void max_sequence(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_max_sequence"_f] = value;
    }
    inline void index_id(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_index_id"_f] = value;
    }
};

struct SYNCSPIRIT_API Folder: proto::impl::changeable::Folder<details::Folder> {
    using parent_t = proto::impl::changeable::Folder<details::Folder>;
    using parent_t::parent_t;
    void add_device(proto::Device value) noexcept;
};

struct SYNCSPIRIT_API ClusterConfig {
    ClusterConfig(details::ClusterConfig* impl_= nullptr): impl{impl_} {}
    details::ClusterConfig* impl;
    void add_folder(proto::Folder value) noexcept;
};


struct SYNCSPIRIT_API Request {
    Request(details::Request* impl_): impl{impl_} {}
    details::Request* impl;
    inline void id(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_id"_f] = value;
    }
    inline void folder(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_folder"_f] = std::string(value);
    }
    inline void name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_name"_f] = std::string(value);
    }
    inline void offset(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_offset"_f] = value;
    }
    inline void size(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_size"_f] = value;
    }
    inline void hash(utils::bytes_view_t value) noexcept {
        using namespace pp;
        auto ptr = (std::byte*)(value.data());
        auto bytes = utils::bytes_t(value.begin(), value.end());
        (*impl)["_hash"_f] = std::move(bytes);
    }
    inline void from_temporary(bool value) noexcept {
        using namespace pp;
        (*impl)["_from_temporary"_f] = value;
    }
    inline void weak_hash(std::uint32_t value) noexcept {
        using namespace pp;
        (*impl)["_weak_hash"_f] = value;
    }
};

struct SYNCSPIRIT_API Response {
    Response(details::Response* impl_): impl{impl_} {}
    inline void id(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_id"_f] = value;
    }
    inline void data(utils::bytes_view_t value) noexcept {
        using namespace pp;
        auto ptr = (std::byte*)(value.data());
        auto bytes = utils::bytes_t(value.begin(), value.end());
        (*impl)["_data"_f] = std::move(bytes);
    }
    inline void code(ErrorCode value) noexcept {
        using namespace pp;
        (*impl)["_code"_f] = value;
    }
    details::Response* impl;
};

struct SYNCSPIRIT_API Close {
    Close(details::Close* impl_): impl{impl_} {}
    details::Close* impl;
    inline void reason(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_reason"_f] = std::string(value);
    }
};

struct SYNCSPIRIT_API IndexBase {
    IndexBase(details::Index* impl_= nullptr): impl{impl_} {}
    inline void folder(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_folder"_f] = std::string(value);
    }
    void add_file(proto::FileInfo file) noexcept;
    FileInfo add_new_file() noexcept;
    details::Index* impl;
};

}

namespace view {

struct SYNCSPIRIT_API Announce {
    Announce(const details::Announce* impl_= nullptr): impl{impl_} {}
    inline bytes_view_t id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                auto& value = opt.value();
                return bytes_view_t(value.begin(), value.end());
            }
        }
        return bytes_view_t{};
    }
    inline std::int64_t instance_id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_instance_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline size_t addresses_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_addresses"_f].size();
        }
        return 0;
    }
    inline std::string_view addresses(size_t i) const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_addresses"_f][i];
        }
        return {};
    }
    const details::Announce* impl;
};

struct SYNCSPIRIT_API Hello {
    Hello(const details::Hello* impl_= nullptr): impl{impl_} {}
    inline std::string_view device_name() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_device_name"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::string_view client_name() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_client_name"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::string_view client_version() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_client_version"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    const details::Hello* impl;
};

struct SYNCSPIRIT_API Header {
    Header(const details::Header* impl_= nullptr): impl{impl_} {}
    MessageType type() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_type"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    MessageCompression compression() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_compression"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    const details::Header* impl;
};

struct SYNCSPIRIT_API Device: proto::impl::view::Device<details::Device> {
    using parent_t = proto::impl::view::Device<details::Device>;
    using parent_t::parent_t;
    inline std::uint64_t index_id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_index_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::int64_t max_sequence() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_max_sequence"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    const details::Device* impl;
};

struct SYNCSPIRIT_API Folder: proto::impl::view::Folder<details::Folder> {
    using parent_t = proto::impl::view::Folder<details::Folder>;
    using parent_t::parent_t;

    inline size_t devices_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_devices"_f].size();
        }
        return 0;
    }
    inline Device devices(size_t i) const noexcept {
        using namespace pp;
        return Device(&(*impl)["_devices"_f].at(i));
    }
};

struct SYNCSPIRIT_API ClusterConfig {
    ClusterConfig(const details::ClusterConfig* impl_= nullptr): impl{impl_} {}

    inline size_t folders_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_folders"_f].size();
        }
        return 0;
    }
    inline Folder folders(size_t i) const noexcept {
        using namespace pp;
        if (impl) {
            return Folder(&(*impl)["_folders"_f][i]);
        }
        return Folder(nullptr);
    }

    const details::ClusterConfig* impl;
};

struct SYNCSPIRIT_API Counter {
    Counter(const details::Counter* impl_= nullptr): impl{impl_} {}
    Counter(const Counter& other): impl{other.impl} { }
    Counter(Counter&& other): impl{nullptr} { (*this) = std::move(other); }
    Counter& operator=(Counter&& other) noexcept { std::swap(impl, other.impl); return *this; }
    Counter& operator=(const Counter& other) noexcept { impl = other.impl; return *this; }

    inline std::uint64_t id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }

    inline std::uint64_t value() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_value"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline const details::Counter& expose() noexcept { return *impl; }
    const details::Counter* impl;
};

struct SYNCSPIRIT_API Vector {
    Vector(const details::Vector* impl_= nullptr): impl{impl_} {}
    inline size_t counters_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_counters"_f].size();
        }
        return 0;
    }
    Counter counters(size_t i) const noexcept {
        using namespace pp;
        auto next = (const details::Counter*)(nullptr);
        if (impl) {
            next = &(*impl)["_counters"_f][i];
        }
        return Counter(next);
    }
    const details::Vector* impl;
};

struct SYNCSPIRIT_API BlockInfo: proto::impl::view::BlockInfo<details::BlockInfo> {
    using parent_t = proto::impl::view::BlockInfo<details::BlockInfo>;
    using parent_t::parent_t;

    inline std::int64_t offset() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_offset"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline bytes_view_t hash() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_hash"_f];
            if (opt) {
                auto& value = opt.value();
                return bytes_view_t(value.begin(), value.end());
            }
        }
        return bytes_view_t{};
    }
    proto::BlockInfo clone() const noexcept;
};

struct SYNCSPIRIT_API FileInfo: proto::impl::view::FileInfo<details::FileInfo> {
    using parent_t = proto::impl::view::FileInfo<details::FileInfo>;
    using parent_t::parent_t;

    inline BlockInfo blocks(size_t i) const noexcept {
        using namespace pp;
        auto bi_impl = (const details::BlockInfo*){nullptr};
        if (impl) {
            bi_impl = &(*impl)["_blocks"_f][i];
        }
        return BlockInfo(bi_impl);
    }
    inline Vector version() const noexcept {
        using namespace pp;
        auto v_impl = (const details::Vector*){nullptr};
        if (impl) {
            auto& opt = (*impl)["_version"_f];
            if (opt) {
                v_impl = &opt.value();
            }
        }
        return Vector(v_impl);
    }
    proto::FileInfo clone() const noexcept;
};

struct SYNCSPIRIT_API IndexBase {
    IndexBase(details::Index* impl_= nullptr): impl{impl_} {}
    inline std::string_view folder() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_folder"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline size_t files_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_files"_f].size();
        }
        return 0;
    }
    inline FileInfo files(size_t i) const noexcept {
        using namespace pp;
        return FileInfo(&(*impl)["_files"_f].at(i));
    }
    details::Index* impl;
};

struct SYNCSPIRIT_API Request {
    Request(const details::Request* impl_): impl{impl_} {}

    inline std::int32_t id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::string_view folder() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_folder"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline std::string_view name() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_name"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline std::int64_t offset() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_offset"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::int32_t size() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_size"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline bytes_view_t hash() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_hash"_f];
            if (opt) {
                auto& value = opt.value();
                return bytes_view_t(value.begin(), value.end());
            }
        }
        return bytes_view_t{};
    }
    inline bool from_temporary() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_from_temporary"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::uint32_t weak_hash() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_weak_hash"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    const details::Request* impl;
};

struct SYNCSPIRIT_API Response {
    Response(const details::Response* impl_): impl{impl_} {}
    const details::Response* impl;
    inline std::int32_t id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline bytes_view_t data() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_data"_f];
            if (opt) {
                auto& value = opt.value();
                return bytes_view_t(value.begin(), value.end());
            }
        }
        return bytes_view_t{};
    }
    inline ErrorCode code() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_code"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
};

struct SYNCSPIRIT_API FileDownloadProgressUpdate: private details::FileDownloadProgressUpdate {
    using parent_t = details::FileDownloadProgressUpdate;
    using parent_t::parent_t;
};

struct SYNCSPIRIT_API DownloadProgress: private details::DownloadProgress {
    using parent_t = details::DownloadProgress;
    using parent_t::parent_t;
};

struct SYNCSPIRIT_API Close {
    Close(const details::Close* impl_): impl{impl_} {}
    inline std::string_view reason() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_reason"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    const details::Close* impl;
};

}

struct SYNCSPIRIT_API Announce: view::Announce, changeable::Announce, private details::Announce {
    template<typename... T>
    Announce(T&&... args): view::Announce(this), changeable::Announce(this), details::Announce(std::forward<T>(args)...) {}
    Announce(Announce&& other): view::Announce(this),changeable::Announce(this), details::Announce(std::move(other.expose())) {}

    Announce& operator=(Announce&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Announce clone() const noexcept { return Announce(*this); }

    using view::Announce::id;
    using view::Announce::instance_id;
    using changeable::Announce::id;
    using changeable::Announce::instance_id;

    static std::optional<Announce> decode(utils::bytes_view_t bytes) noexcept;
    inline const details::Announce& expose() const noexcept { return *this; }
    inline details::Announce& expose() noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    Announce(const Announce& other): view::Announce(this),changeable::Announce(this), details::Announce(other.expose()) {}
};

struct SYNCSPIRIT_API Hello: view::Hello, changeable::Hello, private details::Hello {
    template<typename... T>
    Hello(T&&... args): view::Hello(this), changeable::Hello(this), details::Hello(std::forward<T>(args)...) {}
    Hello(Hello&& other): view::Hello(this),changeable::Hello(this), details::Hello(std::move(other.expose())) {}

    Hello& operator=(Hello&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Hello clone() const noexcept { return Hello(*this); }

    using view::Hello::device_name;
    using view::Hello::client_name;
    using view::Hello::client_version;
    using changeable::Hello::device_name;
    using changeable::Hello::client_name;
    using changeable::Hello::client_version;

    static std::optional<Hello> decode(utils::bytes_view_t bytes) noexcept;
    inline details::Hello& expose() noexcept { return *this; }
    inline const details::Hello& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    Hello(const Hello& other): view::Hello(this),changeable::Hello(this), details::Hello(other.expose()) {}
};

struct SYNCSPIRIT_API Header: view::Header, changeable::Header, private details::Header {
    template<typename... T>
    Header(T&&... args): view::Header(this), changeable::Header(this), details::Header(std::forward<T>(args)...) {}
    Header(Header&& other): view::Header(this),changeable::Header(this), details::Header(std::move(other.expose())) {}

    Header& operator=(Header&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Header clone() const noexcept { return Header(*this); }

    using view::Header::type;
    using view::Header::compression;
    using changeable::Header::type;
    using changeable::Header::compression;

    static std::optional<Header> decode(utils::bytes_view_t bytes) noexcept;
    inline details::Header& expose() noexcept { return *this; }
    inline const details::Header& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    Header(const Header& other): view::Header(this),changeable::Header(this), details::Header(other.expose()) {}
};

struct SYNCSPIRIT_API ClusterConfig: view::ClusterConfig, changeable::ClusterConfig, private details::ClusterConfig {
    template<typename... T>
    ClusterConfig(T&&... args): view::ClusterConfig(this), changeable::ClusterConfig(this), details::ClusterConfig(std::forward<T>(args)...) {}
    ClusterConfig(ClusterConfig&& other): view::ClusterConfig(this),changeable::ClusterConfig(this), details::ClusterConfig(std::move(other.expose())) {}

    ClusterConfig& operator=(ClusterConfig&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    ClusterConfig clone() const noexcept { return ClusterConfig(*this); }

    static std::optional<ClusterConfig> decode(utils::bytes_view_t bytes) noexcept;
    inline details::ClusterConfig& expose() noexcept { return *this; }
    inline const details::ClusterConfig& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    ClusterConfig(const ClusterConfig& other): view::ClusterConfig(this),changeable::ClusterConfig(this), details::ClusterConfig(other.expose()) {}
};

struct SYNCSPIRIT_API Counter: view::Counter, changeable::Counter, private details::Counter {
    template<typename... T>
    Counter(T&&... args): view::Counter(this),changeable::Counter(this), details::Counter(std::forward<T>(args)...) {}
    Counter(Counter&& other): view::Counter(this),changeable::Counter(this), details::Counter(std::move(other.expose())) {}

    Counter& operator=(Counter&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Counter clone() const noexcept { return Counter(*this); }

    using view::Counter::id;
    using view::Counter::value;
    using changeable::Counter::id;
    using changeable::Counter::value;

    inline details::Counter& expose() noexcept { return *this; }
    inline const details::Counter& expose() const noexcept { return *this; }
    private:
    Counter(const Counter& other): view::Counter(this),changeable::Counter(this), details::Counter(other.expose()) {}
};

struct SYNCSPIRIT_API Device: view::Device, changeable::Device, private details::Device {
    template<typename... T>
    Device(T&&... args): view::Device(this), changeable::Device(this), details::Device(std::forward<T>(args)...) {}
    Device(Device&& other): view::Device(this),changeable::Device(this), details::Device(std::move(other.expose())) {}

    Device& operator=(Device&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Device clone() const noexcept { return Device(*this); }

    using view::Device::id;
    using view::Device::name;
    using view::Device::addresses;
    using view::Device::compression;
    using view::Device::cert_name;
    using view::Device::max_sequence;
    using view::Device::introducer;
    using view::Device::index_id;
    using view::Device::skip_introduction_removals;
    using changeable::Device::id;
    using changeable::Device::name;
    using changeable::Device::compression;
    using changeable::Device::cert_name;
    using changeable::Device::introducer;
    using changeable::Device::index_id;
    using changeable::Device::max_sequence;
    using changeable::Device::skip_introduction_removals;

    inline details::Device& expose() noexcept { return *this; }
    inline const details::Device& expose() const noexcept { return *this; }
    private:
    Device(const Device& other): view::Device(this),changeable::Device(this), details::Device(other.expose()) {}
};

struct SYNCSPIRIT_API BlockInfo: view::BlockInfo, changeable::BlockInfo, private details::BlockInfo {
    template<typename... T>
    BlockInfo(T&&... args): view::BlockInfo(this), changeable::BlockInfo(this), details::BlockInfo(std::forward<T>(args)...) {}
    BlockInfo(BlockInfo&& other): view::BlockInfo(this),changeable::BlockInfo(this), details::BlockInfo(std::move(other.expose())) {}

    BlockInfo& operator=(BlockInfo&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    BlockInfo clone() const noexcept { return BlockInfo(*this); }

    using view::BlockInfo::offset;
    using view::BlockInfo::size;
    using view::BlockInfo::hash;
    using view::BlockInfo::weak_hash;

    using changeable::BlockInfo::offset;
    using changeable::BlockInfo::size;
    using changeable::BlockInfo::hash;
    using changeable::BlockInfo::weak_hash;

    inline details::BlockInfo& expose() noexcept { return *this; }
    inline const details::BlockInfo& expose() const noexcept { return *this; }
    private:
    BlockInfo(const BlockInfo& other): view::BlockInfo(this),changeable::BlockInfo(this), details::BlockInfo(other.expose()) {}
};

struct SYNCSPIRIT_API FileInfo: view::FileInfo, changeable::FileInfo, private details::FileInfo {
    template<typename... T>
    FileInfo(T&&... args): view::FileInfo(this), changeable::FileInfo(this), details::FileInfo(std::forward<T>(args)...) {}
    FileInfo(FileInfo&& other): view::FileInfo(this),changeable::FileInfo(this), details::FileInfo(std::move(other.expose())) {}

    FileInfo& operator=(FileInfo&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    FileInfo clone() const noexcept { return FileInfo(*this); }

    using view::FileInfo::size;
    using view::FileInfo::name;
    using view::FileInfo::type;
    using view::FileInfo::block_size;
    using view::FileInfo::deleted;
    using view::FileInfo::invalid;
    using view::FileInfo::no_permissions;
    using view::FileInfo::permissions;
    using view::FileInfo::symlink_target;
    using view::FileInfo::modified_s;
    using view::FileInfo::modified_ns;
    using view::FileInfo::modified_by;
    using view::FileInfo::sequence;
    using view::FileInfo::blocks_size;

    using changeable::FileInfo::size;
    using changeable::FileInfo::name;
    using changeable::FileInfo::type;
    using changeable::FileInfo::block_size;
    using changeable::FileInfo::deleted;
    using changeable::FileInfo::invalid;
    using changeable::FileInfo::no_permissions;
    using changeable::FileInfo::permissions;
    using changeable::FileInfo::symlink_target;
    using changeable::FileInfo::modified_s;
    using changeable::FileInfo::modified_ns;
    using changeable::FileInfo::modified_by;
    using changeable::FileInfo::sequence;

    inline details::FileInfo& expose() noexcept { return *this; }
    inline const details::FileInfo& expose() const noexcept { return *this; }
    private:
    FileInfo(const FileInfo& other): view::FileInfo(this),changeable::FileInfo(this), details::FileInfo(other.expose()) {}
};

struct SYNCSPIRIT_API Folder: view::Folder, changeable::Folder, private details::Folder {
    template<typename... T>
    Folder(T&&... args): view::Folder(this), changeable::Folder(this), details::Folder(std::forward<T>(args)...) {}
    Folder(Folder&& other): view::Folder(this),changeable::Folder(this), details::Folder(std::move(other.expose())) {}

    Folder& operator=(Folder&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Folder clone() const noexcept { return Folder(*this); }

    using view::Folder::id;
    using view::Folder::label;
    using view::Folder::read_only;
    using view::Folder::ignore_permissions;
    using view::Folder::ignore_delete;
    using view::Folder::disable_temp_indexes;
    using view::Folder::paused;

    using changeable::Folder::id;
    using changeable::Folder::label;
    using changeable::Folder::read_only;
    using changeable::Folder::ignore_permissions;
    using changeable::Folder::ignore_delete;
    using changeable::Folder::disable_temp_indexes;
    using changeable::Folder::paused;

    inline details::Folder& expose() noexcept { return *this; }
    inline const details::Folder& expose() const noexcept { return *this; }
    private:
    Folder(const Folder& other): view::Folder(this),changeable::Folder(this), details::Folder(other.expose()) {}
};

struct SYNCSPIRIT_API Vector: view::Vector, changeable::Vector, private details::Vector {
    template<typename... T>
    Vector(T&&... args): view::Vector(this), changeable::Vector(this), details::Vector(std::forward<T>(args)...) {}
    Vector(Vector&& other): view::Vector(this),changeable::Vector(this), details::Vector(std::move(other.expose())) {}

    Vector& operator=(Vector&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Vector clone() const noexcept { return Vector(*this); }

    inline details::Vector& expose() noexcept { return *this; }
    inline const details::Vector& expose() const noexcept { return *this; }
    private:
    Vector(const Vector& other): view::Vector(this),changeable::Vector(this), details::Vector(other.expose()) {}
};

struct SYNCSPIRIT_API IndexBase: view::IndexBase, changeable::IndexBase, private details::Index {
    template<typename... T>
    IndexBase(T&&... args): view::IndexBase(this), changeable::IndexBase(this), details::Index(std::forward<T>(args)...) {}
    IndexBase(IndexBase&& other): view::IndexBase(this),changeable::IndexBase(this), details::Index(std::move(other.expose())) {}

    IndexBase& operator=(IndexBase&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    IndexBase clone() const noexcept { return IndexBase(*this); }

    using view::IndexBase::folder;
    using changeable::IndexBase::folder;

    static std::optional<IndexBase> decode(utils::bytes_view_t bytes) noexcept;
    inline details::Index& expose() noexcept { return *this; }
    inline const details::Index& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    IndexBase(const IndexBase& other): view::IndexBase(this),changeable::IndexBase(this), details::Index(other.expose()) {}
};

struct SYNCSPIRIT_API Index: IndexBase {};
struct SYNCSPIRIT_API IndexUpdate: IndexBase {};

struct SYNCSPIRIT_API Request: view::Request, changeable::Request, private details::Request {
    template<typename... T>
    Request(T&&... args): view::Request(this), changeable::Request(this), details::Request(std::forward<T>(args)...) {}
    Request(Request&& other): view::Request(this),changeable::Request(this), details::Request(std::move(other.expose())) {}

    Request& operator=(Request&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Request clone() const noexcept { return Request(*this); }

    using view::Request::id;
    using view::Request::folder;
    using view::Request::name;
    using view::Request::offset;
    using view::Request::size;
    using view::Request::hash;
    using view::Request::from_temporary;
    using view::Request::weak_hash;
    using changeable::Request::id;
    using changeable::Request::folder;
    using changeable::Request::name;
    using changeable::Request::offset;
    using changeable::Request::size;
    using changeable::Request::hash;
    using changeable::Request::from_temporary;
    using changeable::Request::weak_hash;

    inline details::Request& expose() noexcept { return *this; }
    inline const details::Request& expose() const noexcept { return *this; }
    static std::optional<Request> decode(utils::bytes_view_t bytes) noexcept;
    utils::bytes_t encode() const noexcept;
    private:
    Request(const Request& other): view::Request(this),changeable::Request(this), details::Request(other.expose()) {}
};

struct SYNCSPIRIT_API Response: view::Response, changeable::Response, private details::Response {
    template<typename... T>
    Response(T&&... args): view::Response(this), changeable::Response(this), details::Response(std::forward<T>(args)...) {}
    Response(Response&& other): view::Response(this),changeable::Response(this), details::Response(std::move(other.expose())) {}

    Response& operator=(Response&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Response clone() const noexcept { return Response(*this); }

    using view::Response::id;
    using view::Response::data;
    using view::Response::code;
    using changeable::Response::id;
    using changeable::Response::data;
    using changeable::Response::code;

    static std::optional<Response> decode(utils::bytes_view_t bytes) noexcept;
    inline details::Response& expose() noexcept { return *this; }
    inline const details::Response& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    Response(const Response& other): view::Response(this),changeable::Response(this), details::Response(other.expose()) {}
};

struct SYNCSPIRIT_API Close: view::Close, changeable::Close, private details::Close {
    template<typename... T>
    Close(T&&... args): view::Close(this), changeable::Close(this), details::Close(std::forward<T>(args)...) {}
    Close(Close&& other): view::Close(this),changeable::Close(this), details::Close(std::move(other.expose())) {}

    Close& operator=(Close&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Close clone() const noexcept { return Close(*this); }

    using view::Close::reason;
    using changeable::Close::reason;

    static std::optional<Close> decode(utils::bytes_view_t bytes) noexcept;
    inline details::Close& expose() noexcept { return *this; }
    inline const details::Close& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    private:
    Close(const Close& other): view::Close(this),changeable::Close(this), details::Close(other.expose()) {}
};

struct SYNCSPIRIT_API Ping: details::Ping {
    template<typename... T>
    Ping(T&&... args): details::Ping(std::forward<T>(args)...) {}
    static std::optional<Ping> decode(utils::bytes_view_t bytes) noexcept;
    utils::bytes_t encode() const noexcept;
};

struct SYNCSPIRIT_API DownloadProgress {
    static std::optional<DownloadProgress> decode(utils::bytes_view_t bytes) noexcept;
    utils::bytes_t encode() const noexcept;
};

}
