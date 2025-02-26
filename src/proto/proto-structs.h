// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include "proto-fwd.hpp"

#if 0
#include "proto-bep.h"
#include "proto-impl.h"
#include "utils/bytes.h"
#include "syncspirit-export.h"
#include <optional>
#include <cassert>

namespace syncspirit::db {

// clang-format off

namespace details {

using IgnoredFolder = pp::message<
    pp::string_field    <"_label",   1>
>;

using Device = pp::message<
    pp::string_field    <"_name",                       1                     >,
    pp::string_field    <"_addresses",                  2, pp::repeated       >,
    pp::enum_field      <"_compression",                3, proto::Compression >,
    pp::string_field    <"_cert_name",                  4                     >,
    pp::bool_field      <"_introducer",                 5                     >,
    pp::bool_field      <"_skip_introduction_removals", 6                     >,
    pp::bool_field      <"_auto_accept",                7                     >,
    pp::bool_field      <"_paused",                     8                     >,
    pp::int64_field     <"_last_seen",                  9                     >
>;

using Folder = pp::message<
    pp::string_field    <"_id",                    1             >,
    pp::string_field    <"_label",                 2             >,
    pp::bool_field      <"_read_only",             3             >,
    pp::bool_field      <"_ignore_permissions",    4             >,
    pp::bool_field      <"_ignore_delete",         5             >,
    pp::bool_field      <"_disable_temp_indexes",  6             >,
    pp::bool_field      <"_paused",                7             >,
    pp::bool_field      <"_scheduled",             8             >,
    pp::string_field    <"_path",                  9             >,
    pp::enum_field      <"_folder_type",          10, FolderType >,
    pp::enum_field      <"_pull_order",           11, PullOrder  >,
    pp::uint32_field    <"_rescan_interval",      12             >
>;

using FolderInfo = pp::message<
    pp::uint64_field    <"_index_id",     1>,
    pp::int64_field     <"_max_sequence", 2>
>;

using PendingFolder = pp::message<
    pp::message_field   <"_folder",      1, Folder    >,
    pp::message_field   <"_folder_info", 2, FolderInfo>
>;

using FileInfo = pp::message<
    pp::string_field    <"_name",            1                                            >,
    pp::enum_field      <"_type",            2, proto::FileInfoType                       >,
    pp::int64_field     <"_size",            3                                            >,
    pp::uint32_field    <"_permissions",     4                                            >,
    pp::int64_field     <"_modified_s",      5                                            >,
    pp::int32_field     <"_modified_ns",     11                                           >,
    pp::uint64_field    <"_modified_by",     12                                           >,
    pp::bool_field      <"_deleted",         6                                            >,
    pp::bool_field      <"_invalid",         7                                            >,
    pp::bool_field      <"_no_permissions",  8                                            >,
    pp::message_field   <"_version",         9, proto::details::Vector                    >,
    pp::int64_field     <"_sequence",        10                                           >,
    pp::int32_field     <"_block_size",      13                                           >,
    pp::string_field    <"_symlink_target",  16                                           >,
    pp::bytes_field     <"_blocks",          17, pp::repeated, proto::impl::bytes_backend_t>
>;

using BlockInfo = pp::message<
    pp::uint32_field   <"_weak_hash", 1>,
    pp::int32_field    <"_size",      2>
>;

using SomeDevice = pp::message<
    pp::string_field <"_name",           1>,
    pp::string_field <"_client_name",    2>,
    pp::string_field <"_client_version", 3>,
    pp::string_field <"_address",        4>,
    pp::int64_field  <"_last_seen",      5>
>;

}

namespace changeable {

struct SYNCSPIRIT_API BlockInfo: proto::impl::changeable::BlockInfo<details::BlockInfo> {
    using parent_t = proto::impl::changeable::BlockInfo<details::BlockInfo>;
    using parent_t::parent_t;
};

struct SYNCSPIRIT_API Device: proto::impl::changeable::Device<details::Device> {
    using parent_t = proto::impl::changeable::Device<details::Device>;
    using parent_t::parent_t;

    inline void auto_accept(bool value) noexcept {
        using namespace pp;
        (*impl)["_auto_accept"_f] = value;
    }
    inline void paused(bool value) noexcept {
        using namespace pp;
        (*impl)["_paused"_f] = value;
    }
    inline void last_seen(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_last_seen"_f] = value;
    }
};

struct SYNCSPIRIT_API FileInfo: proto::impl::changeable::FileInfo<details::FileInfo> {
    using parent_t = proto::impl::changeable::FileInfo<details::FileInfo>;
    using parent_t::parent_t;

    inline void add_block(utils::bytes_view_t hash) noexcept {
        using namespace pp;
        auto bytes = utils::bytes_t(hash.begin(), hash.end());
        (*impl)["_blocks"_f].emplace_back(std::move(bytes));
    }
    proto::changeable::Vector mutable_version() noexcept;
};

struct SYNCSPIRIT_API Folder: proto::impl::changeable::Folder<details::Folder> {
    using parent_t = proto::impl::changeable::Folder<details::Folder>;
    using parent_t::parent_t;
    inline void rescan_interval(std::uint32_t value) noexcept {
        using namespace pp;
        (*impl)["_rescan_interval"_f] = value;
    }
    inline void scheduled(bool value) noexcept {
        using namespace pp;
        (*impl)["_scheduled"_f] = value;
    }
    inline void path(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_path"_f] = std::string(value);
    }
    inline void folder_type(FolderType value) noexcept {
        using namespace pp;
        (*impl)["_folder_type"_f] = value;
    }
    inline void pull_order(PullOrder value) noexcept {
        using namespace pp;
        (*impl)["_pull_order"_f] = value;
    }
};

struct SYNCSPIRIT_API FolderInfo {
    FolderInfo(details::FolderInfo* impl_): impl{impl_}{ assert(impl); }
    inline void index_id(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_index_id"_f] = value;
    }
    inline void max_sequence(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_max_sequence"_f] = value;
    }
    details::FolderInfo* impl;
};

struct SYNCSPIRIT_API PendingFolder {
    PendingFolder(details::PendingFolder* impl_): impl{impl_}{ assert(impl); }
    inline Folder mutable_folder() noexcept {
        using namespace pp;
        return Folder(&(*impl)["_folder"_f].value());
    }

    inline FolderInfo mutable_folder_info() noexcept {
        using namespace pp;
        return FolderInfo(&(*impl)["_folder_info"_f].value());
    }
    details::PendingFolder* impl;
};

struct SYNCSPIRIT_API IgnoredFolder {
    IgnoredFolder(details::IgnoredFolder* impl_): impl{impl_}{ assert(impl); }
    inline void label(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_label"_f] = std::string(value);
    }
    details::IgnoredFolder* impl;
};

struct SYNCSPIRIT_API SomeDevice {
    SomeDevice(details::SomeDevice* impl_): impl{impl_}{ assert(impl); }
    inline void name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_name"_f] = std::string(value);
    }
    inline void client_name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_client_name"_f] = std::string(value);
    }
    inline void client_version(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_client_version"_f] = std::string(value);
    }
    inline void address(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_address"_f] = std::string(value);
    }
    inline void last_seen(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_last_seen"_f] = value;
    }
    details::SomeDevice* impl;
};

};

namespace view {

struct SYNCSPIRIT_API BlockInfo: proto::impl::view::BlockInfo<details::BlockInfo> {
    using parent_t = proto::impl::view::BlockInfo<details::BlockInfo>;
    using parent_t::parent_t;
};

struct SYNCSPIRIT_API Device: proto::impl::view::Device<details::Device> {
    using parent_t = proto::impl::view::Device<details::Device>;
    using parent_t::parent_t;

    inline bool auto_accept() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_auto_accept"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool paused() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_paused"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline std::int64_t last_seen() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_last_seen"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
};

struct SYNCSPIRIT_API FolderInfo {
    FolderInfo(const details::FolderInfo* impl_): impl{impl_}{}
    inline std::uint64_t index_id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_index_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::int64_t max_sequence() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_max_sequence"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    const details::FolderInfo* impl;
};


struct SYNCSPIRIT_API Folder: proto::impl::view::Folder<details::Folder> {
    using parent_t = proto::impl::view::Folder<details::Folder>;
    using parent_t::parent_t;

    inline bool scheduled() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_scheduled"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::string_view path() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_path"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline FolderType folder_type() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_folder_type"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline PullOrder pull_order() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_pull_order"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::uint32_t rescan_interval() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_rescan_interval"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
};

struct SYNCSPIRIT_API FileInfo: proto::impl::view::FileInfo<details::FileInfo> {
    using parent_t = proto::impl::view::FileInfo<details::FileInfo>;
    using parent_t::parent_t;

    inline utils::bytes_view_t block(size_t i) const noexcept {
        using namespace pp;
        if (impl) {
            auto& b = (*impl)["_blocks"_f][i];
            auto ptr = reinterpret_cast<const unsigned char*>(b.data());
            return utils::bytes_view_t(ptr, b.size());
        }
        return utils::bytes_view_t{};
    }
    inline proto::view::Vector version() const noexcept {
        using namespace pp;
        auto v_impl = (const proto::details::Vector*){nullptr};
        if (impl) {
            auto& opt = (*impl)["_version"_f];
            if (opt) {
                v_impl = &opt.value();
            }
        }
        return proto::view::Vector(v_impl);
    }

    const details::FileInfo* impl;
};

struct SYNCSPIRIT_API PendingFolder {
    PendingFolder(const details::PendingFolder* impl_): impl{impl_}{}
    const details::PendingFolder* impl;
    inline Folder folder() const {
        using namespace pp;
        auto f_impl = (const details::Folder*){nullptr};
        if (impl) {
            auto& opt = (*impl)["_folder"_f];
            if (opt) {
                f_impl = &opt.value();
            }
        }
        return view::Folder(f_impl);
    }
    inline FolderInfo folder_info() const {
        using namespace pp;
        auto f_impl = (const details::FolderInfo*){nullptr};
        if (impl) {
            auto& opt = (*impl)["_folder_info"_f];
            if (opt) {
                f_impl = &opt.value();
            }
        }
        return view::FolderInfo(f_impl);
    }
};

struct SYNCSPIRIT_API IgnoredFolder {
    IgnoredFolder(const details::IgnoredFolder* impl_): impl{impl_}{}
    inline std::string_view label() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_label"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    const details::IgnoredFolder* impl;
};

struct SYNCSPIRIT_API SomeDevice {
    SomeDevice(const details::SomeDevice* impl_): impl{impl_}{}
    inline std::string_view name() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_name"_f];
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
    inline std::string_view address() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_address"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }
    inline std::uint64_t last_seen() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_last_seen"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }

    const details::SomeDevice* impl;
};

}

struct SYNCSPIRIT_API Device: view::Device, changeable::Device, private details::Device {
    template<typename... T>
    Device(T&&... args): view::Device(this), changeable::Device(this), details::Device(std::forward<T>(args)...) {}
    Device(Device&& other): view::Device(this),changeable::Device(this), details::Device(std::move(other.expose())) {}

    Device& operator=(Device&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Device clone() const noexcept { return Device(*this); }

    using view::Device::name;
    using view::Device::compression;
    using view::Device::cert_name;
    using view::Device::introducer;
    using view::Device::skip_introduction_removals;
    using view::Device::auto_accept;
    using view::Device::paused;
    using view::Device::last_seen;

    using changeable::Device::name;
    using changeable::Device::compression;
    using changeable::Device::cert_name;
    using changeable::Device::introducer;
    using changeable::Device::skip_introduction_removals;
    using changeable::Device::auto_accept;
    using changeable::Device::paused;
    using changeable::Device::last_seen;

    inline details::Device& expose() noexcept { return *this; }
    inline const details::Device& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<Device> decode(utils::bytes_view_t bytes) noexcept;

    private:
    Device(const Device& other): view::Device(this),changeable::Device(this), details::Device(other.expose()) {}
};

struct SYNCSPIRIT_API Folder: view::Folder, changeable::Folder, private details::Folder {
    template<typename... T>
    Folder(T&&... args): view::Folder(this), changeable::Folder(this), details::Folder(std::forward<T>(args)...) {}
    Folder(Folder&& other): view::Folder(this), changeable::Folder(this), details::Folder(std::move(other.expose())) {}

    Folder& operator=(Folder&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    Folder clone() const noexcept { return Folder(*this); }

    using view::Folder::id;
    using view::Folder::label;
    using view::Folder::read_only;
    using view::Folder::ignore_permissions;
    using view::Folder::ignore_delete;
    using view::Folder::disable_temp_indexes;
    using view::Folder::paused;
    using view::Folder::scheduled;
    using view::Folder::path;
    using view::Folder::folder_type;
    using view::Folder::pull_order;
    using view::Folder::rescan_interval;

    using changeable::Folder::id;
    using changeable::Folder::label;
    using changeable::Folder::read_only;
    using changeable::Folder::ignore_permissions;
    using changeable::Folder::ignore_delete;
    using changeable::Folder::disable_temp_indexes;
    using changeable::Folder::paused;
    using changeable::Folder::scheduled;
    using changeable::Folder::path;
    using changeable::Folder::folder_type;
    using changeable::Folder::pull_order;
    using changeable::Folder::rescan_interval;

    inline details::Folder& expose() noexcept { return *this; }
    inline const details::Folder& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<Folder> decode(utils::bytes_view_t bytes) noexcept;
    private:
    Folder(const Folder& other): view::Folder(this), changeable::Folder(this), details::Folder(other.expose()) {}
};

struct SYNCSPIRIT_API FolderInfo: view::FolderInfo, changeable::FolderInfo, private details::FolderInfo {
    template<typename... T>
    FolderInfo(T&&... args): view::FolderInfo(this), changeable::FolderInfo(this), details::FolderInfo(std::forward<T>(args)...) {}
    FolderInfo(FolderInfo&& other): view::FolderInfo(this), changeable::FolderInfo(this), details::FolderInfo(std::move(other.expose())) {}

    FolderInfo& operator=(FolderInfo&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    FolderInfo clone() const noexcept { return FolderInfo(*this); }

    using view::FolderInfo::index_id;
    using view::FolderInfo::max_sequence;
    using changeable::FolderInfo::index_id;
    using changeable::FolderInfo::max_sequence;

    inline details::FolderInfo& expose() noexcept { return *this; }
    inline const details::FolderInfo& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<FolderInfo> decode(utils::bytes_view_t bytes) noexcept;
    private:
    FolderInfo(const FolderInfo& other): view::FolderInfo(this), changeable::FolderInfo(this), details::FolderInfo(other.expose()) {}
};

struct SYNCSPIRIT_API FileInfo: view::FileInfo, changeable::FileInfo, private details::FileInfo {
    template<typename... T>
    FileInfo(T&&... args): view::FileInfo(this), changeable::FileInfo(this), details::FileInfo(std::forward<T>(args)...) {}
    FileInfo(FileInfo&& other): view::FileInfo(this), changeable::FileInfo(this), details::FileInfo(std::move(other.expose())) {}

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
    static std::optional<FileInfo> decode(utils::bytes_view_t bytes) noexcept;
    utils::bytes_t encode() const noexcept;
    private:
    FileInfo(const FileInfo& other): view::FileInfo(this), changeable::FileInfo(this), details::FileInfo(other.expose()) {}
};

struct SYNCSPIRIT_API IgnoredFolder: view::IgnoredFolder, changeable::IgnoredFolder, private details::IgnoredFolder {
    template<typename... T>
    IgnoredFolder(T&&... args): view::IgnoredFolder(this), changeable::IgnoredFolder(this), details::IgnoredFolder(std::forward<T>(args)...) {}
    IgnoredFolder(IgnoredFolder&& other): view::IgnoredFolder(this), changeable::IgnoredFolder(this), details::IgnoredFolder(std::move(other.expose())) {}

    IgnoredFolder& operator=(IgnoredFolder&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    IgnoredFolder clone() const noexcept { return IgnoredFolder(*this); }

    using view::IgnoredFolder::label;
    using changeable::IgnoredFolder::label;

    inline details::IgnoredFolder& expose() noexcept { return *this; }
    inline const details::IgnoredFolder& expose() const noexcept { return *this; }
    static std::optional<IgnoredFolder> decode(utils::bytes_view_t bytes) noexcept;
    utils::bytes_t encode() const noexcept;
    private:
    IgnoredFolder(const IgnoredFolder& other): view::IgnoredFolder(this), changeable::IgnoredFolder(this), details::IgnoredFolder(other.expose()) {}
};

struct SYNCSPIRIT_API BlockInfo: view::BlockInfo, changeable::BlockInfo, private details::BlockInfo {
    template<typename... T>
    BlockInfo(T&&... args): view::BlockInfo(this), changeable::BlockInfo(this), details::BlockInfo(std::forward<T>(args)...) {}
    BlockInfo(BlockInfo&& other): view::BlockInfo(this), changeable::BlockInfo(this), details::BlockInfo(std::move(other.expose())) {}

    BlockInfo& operator=(BlockInfo&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    BlockInfo clone() const noexcept { return BlockInfo(*this); }

    using view::BlockInfo::size;
    using view::BlockInfo::weak_hash;

    using changeable::BlockInfo::size;
    using changeable::BlockInfo::weak_hash;

    inline details::BlockInfo& expose() noexcept { return *this; }
    inline const details::BlockInfo& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<BlockInfo> decode(utils::bytes_view_t bytes) noexcept;
    private:
    BlockInfo(const BlockInfo& other): view::BlockInfo(this), changeable::BlockInfo(this), details::BlockInfo(other.expose()) {}
};

struct SYNCSPIRIT_API PendingFolder: view::PendingFolder, changeable::PendingFolder, private details::PendingFolder {
    template<typename... T>
    PendingFolder(T&&... args): view::PendingFolder(this), changeable::PendingFolder(this), details::PendingFolder(std::forward<T>(args)...) {}
    PendingFolder(PendingFolder&& other): view::PendingFolder(this), changeable::PendingFolder(this), details::PendingFolder(std::move(other.expose())) {}

    PendingFolder& operator=(PendingFolder&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    PendingFolder clone() const noexcept { return PendingFolder(*this); }

    inline details::PendingFolder& expose() noexcept { return *this; }
    inline const details::PendingFolder& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<PendingFolder> decode(utils::bytes_view_t bytes) noexcept;
    private:
    PendingFolder(const PendingFolder& other): view::PendingFolder(this), changeable::PendingFolder(this), details::PendingFolder(other.expose()) {}
};

struct SYNCSPIRIT_API SomeDevice: view::SomeDevice, changeable::SomeDevice, private details::SomeDevice {
    template<typename... T>
    SomeDevice(T&&... args): view::SomeDevice(this), changeable::SomeDevice(this), details::SomeDevice(std::forward<T>(args)...) {}
    SomeDevice(SomeDevice&& other): view::SomeDevice(this), changeable::SomeDevice(this), details::SomeDevice(std::move(other.expose())) {}

    SomeDevice& operator=(SomeDevice&& other) noexcept { expose() = std::move(other.expose()); return *this; }
    SomeDevice clone() const noexcept { return SomeDevice(*this); }


    using view::SomeDevice::name;
    using view::SomeDevice::client_name;
    using view::SomeDevice::client_version;
    using view::SomeDevice::address;
    using view::SomeDevice::last_seen;

    using changeable::SomeDevice::name;
    using changeable::SomeDevice::client_name;
    using changeable::SomeDevice::client_version;
    using changeable::SomeDevice::address;
    using changeable::SomeDevice::last_seen;

    inline details::SomeDevice& expose() noexcept { return *this; }
    inline const details::SomeDevice& expose() const noexcept { return *this; }
    utils::bytes_t encode() const noexcept;
    static std::optional<SomeDevice> decode(utils::bytes_view_t bytes) noexcept;
    private:
    SomeDevice(const SomeDevice& other): view::SomeDevice(this), changeable::SomeDevice(this), details::SomeDevice(other.expose()) {}
};

// clang-format on

}
#endif
