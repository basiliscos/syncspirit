// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once

#include <cassert>
#include <protopuf/message.h>
#include "proto-fwd.hpp"
#include "syncspirit-export.h"
#include "utils/bytes.h"

namespace syncspirit::proto::impl {

template<typename T, typename B> std::optional<T> generic_decode(utils::bytes_view_t bytes) noexcept {
    using coder_t = pp::message_coder<B>;
    auto ptr = (std::byte*)bytes.data();
    auto span = std::span<std::byte>(ptr, bytes.size());
    auto opt = coder_t::decode(span);
    if (opt) {
        auto& impl = opt.value().first;
        return T(std::move(impl));
    }
    return {};
}

template<typename B, typename T> utils::bytes_t generic_encode(const T& item) noexcept {
    using coder_t = pp::message_coder<B>;
    using skipper_t = pp::skipper<coder_t>;
    auto &message = item.expose();
    auto size = skipper_t::encode_skip(message);
    auto storage = utils::bytes_t(size);
    auto bytes = pp::bytes((std::byte*)storage.data(), size);
    coder_t::template encode<pp::unsafe_mode>(message, bytes);
    return storage;
}

using bytes_backend_t = std::vector<std::vector<unsigned char>>;

namespace changeable {

template<typename T>
struct BlockInfo {
    BlockInfo(T* impl_): impl{impl_} {}
    inline void weak_hash(std::uint32_t value) noexcept {
        using namespace pp;
        (*impl)["_weak_hash"_f] = value;
    }
    inline void size(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_size"_f] = value;
    }

    T* impl;
};

template<typename T>
struct Device {
    Device(T* impl_): impl{impl_} {}

    inline void name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_name"_f] = std::string(value);
    }
    inline void compression(proto::Compression value) noexcept {
        using namespace pp;
        (*impl)["_compression"_f] = value;
    }
    inline void cert_name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_cert_name"_f] = std::string(value);
    }
    inline void introducer(bool value) noexcept {
        using namespace pp;
        (*impl)["_introducer"_f] = value;
    }
    inline void skip_introduction_removals(bool value) noexcept {
        using namespace pp;
        (*impl)["_skip_introduction_removals"_f] = value;
    }
    inline void add_addresses(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_addresses"_f].emplace_back(std::string(value));
    }

    T* impl;
};

template<typename T>
struct Folder {
    Folder(T* impl_): impl{impl_}{ assert(impl); }
    // Folder(const Folder& other): impl{other.impl} { }
    // Folder(Folder&& other): impl{nullptr} { (*this) = std::move(other); }
    // Folder& operator=(Folder&& other) noexcept { std::swap(impl, other.impl); return *this; }
    // Folder& operator=(const Folder& other) noexcept { impl = other.impl; return *this; }

    inline void id(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_id"_f] = std::string(value);
    }
    inline void label(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_label"_f] = std::string(value);
    }
    inline void read_only(bool value) noexcept {
        using namespace pp;
        (*impl)["_read_only"_f] = value;
    }
    inline void ignore_permissions(bool value) noexcept {
        using namespace pp;
        (*impl)["_ignore_permissions"_f] = value;
    }
    inline void ignore_delete(bool value) noexcept {
        using namespace pp;
        (*impl)["_ignore_delete"_f] = value;
    }
    inline void disable_temp_indexes(bool value) noexcept {
        using namespace pp;
        (*impl)["_disable_temp_indexes"_f] = value;
    }
    inline void paused(bool value) noexcept {
        using namespace pp;
        (*impl)["_paused"_f] = value;
    }
    inline void max_sequence(std::uint32_t value) noexcept {
        using namespace pp;
        (*impl)["_max_sequence"_f] = value;
    }
    T* impl;
};

template<typename T>
struct FileInfo {
    FileInfo(T* impl_): impl{impl_} {}

    inline void name(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_name"_f] = std::string(value);
    }
    inline void type(FileInfoType value) noexcept {
        using namespace pp;
        (*impl)["_type"_f] = value;
    }
    inline void size(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_size"_f] = value;
    }
    inline void permissions(std::uint32_t value) noexcept {
        using namespace pp;
        (*impl)["_permissions"_f] = value;
    }
    inline void modified_s(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_modified_s"_f] = value;
    }
    inline void modified_ns(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_modified_ns"_f] = value;
    }
    inline void modified_by(std::uint64_t value) noexcept {
        using namespace pp;
        (*impl)["_modified_by"_f] = value;
    }
    inline void deleted(bool value) noexcept {
        using namespace pp;
        (*impl)["_deleted"_f] = value;
    }
    inline void invalid(bool value) noexcept {
        using namespace pp;
        (*impl)["_invalid"_f] = value;
    }
    inline void no_permissions(bool value) noexcept {
        using namespace pp;
        (*impl)["_no_permissions"_f] = value;
    }
    inline void sequence(std::int64_t value) noexcept {
        using namespace pp;
        (*impl)["_sequence"_f] = value;
    }
    inline void block_size(std::int32_t value) noexcept {
        using namespace pp;
        (*impl)["_block_size"_f] = value;
    }
    inline void symlink_target(std::string_view value) noexcept {
        using namespace pp;
        (*impl)["_symlink_target"_f] = std::string(value);
    }
    T* impl;
};

}

namespace view {

template<typename T>
struct BlockInfo {
    BlockInfo(const T* impl_): impl{impl_} {}
    inline std::int32_t size() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_size"_f];
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
        return {};
    }
    const T* impl;
};

template<typename T>
struct Device {
    Device(const T* impl_): impl{impl_} {}
    inline utils::bytes_view_t id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                auto& value = opt.value();
                auto ptr = (const unsigned char*)value.data();
                return {ptr, value.size()};
            }
        }
        return {};
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
    inline proto::Compression compression() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_compression"_f];
            if (opt) {
                return opt.value();
            }
        }
        return proto::Compression::NEVER;
    }
    inline std::string_view cert_name() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_cert_name"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline bool introducer() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_introducer"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool skip_introduction_removals() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_skip_introduction_removals"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    const T* impl;
};

template<typename T>
struct Folder {
    Folder(const T* impl_): impl{impl_} {}
    // Folder(const Folder& other): impl{other.impl} { }
    // Folder(Folder&& other): impl{nullptr} { (*this) = std::move(other); }
    // Folder& operator=(Folder&& other) noexcept { std::swap(impl, other.impl); return *this; }
    // Folder& operator=(const Folder& other) noexcept { impl = other.impl; return *this; }

    inline std::string_view id() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_id"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline std::string_view label() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_label"_f];
            if (opt) {
                return opt.value();
            }
        }
        return "";
    }
    inline bool read_only() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_read_only"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool ignore_permissions() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_ignore_permissions"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool ignore_delete() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_ignore_delete"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool disable_temp_indexes() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_disable_temp_indexes"_f];
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
    const T* impl;
};

template<typename T>
struct SYNCSPIRIT_API FileInfo {
    FileInfo(const T* impl_): impl{impl_} {assert(impl_);}

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
    inline FileInfoType type() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_type"_f];
            if (opt) {
                return opt.value();
            }
        }
        return FileInfoType::FILE;
    }
    inline int64_t size() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_size"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::uint32_t permissions() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_permissions"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::int64_t modified_s() const noexcept {
        using namespace pp;
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_modified_s"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::int32_t modified_ns() const noexcept {
        using namespace pp;
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_modified_ns"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline std::uint64_t modified_by() const noexcept {
        using namespace pp;
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_modified_by"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline bool deleted() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_deleted"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool invalid() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_invalid"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline bool no_permissions() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_no_permissions"_f];
            if (opt) {
                return opt.value();
            }
        }
        return false;
    }
    inline std::int64_t sequence() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_sequence"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline int32_t block_size() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_block_size"_f];
            if (opt) {
                return opt.value();
            }
        }
        return 0;
    }
    inline size_t blocks_size() const noexcept {
        using namespace pp;
        if (impl) {
            return (*impl)["_blocks"_f].size();
        }
        return 0;
    }
    inline std::string_view symlink_target() const noexcept {
        using namespace pp;
        if (impl) {
            auto& opt = (*impl)["_symlink_target"_f];
            if (opt) {
                return opt.value();
            }
        }
        return {};
    }

    const T* impl;
};

}

}
