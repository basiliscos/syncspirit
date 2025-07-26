// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-helpers-bep.h"
#include "proto-helpers-impl.hpp"

namespace syncspirit::proto {

using namespace syncspirit::details;

template <typename T> size_t generic_estimate(const T &object) {
    using coder_t = pp::message_coder<T>;
    using skipper_t = pp::skipper<coder_t>;
    return skipper_t::encode_skip(object);
}

template <typename T> void encode_unsafe(const T &object, utils::bytes_view_t storage) {
    using coder_t = pp::message_coder<T>;
    using skipper_t = pp::skipper<coder_t>;
    auto bytes = pp::bytes((std::byte *)storage.data(), storage.size());
    coder_t::template encode<pp::unsafe_mode>(object, bytes);
}

int decode(utils::bytes_view_t bytes, Announce &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Announce &object) { return generic_estimate(object); }

void encode(const Announce &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, BlockInfo &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const BlockInfo &object) { return generic_estimate(object); }

void encode(const BlockInfo &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Close &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Close &object) { return generic_estimate(object); }

void encode(const Close &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, ClusterConfig &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const ClusterConfig &object) { return generic_estimate(object); }

void encode(const ClusterConfig &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Device &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Device &object) { return generic_estimate(object); }

void encode(const Device &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, DownloadProgress &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const DownloadProgress &object) { return generic_estimate(object); }

void encode(const DownloadProgress &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, FileDownloadProgressUpdate &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const FileDownloadProgressUpdate &object) { return generic_estimate(object); }

void encode(const FileDownloadProgressUpdate &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, FileInfo &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const FileInfo &object) { return generic_estimate(object); }

void encode(const FileInfo &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Folder &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Folder &object) { return generic_estimate(object); }

void encode(const Folder &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Header &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Header &object) { return generic_estimate(object); }

void encode(const Header &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Hello &dest) { return generic_decode(bytes, dest); }

int decode(utils::bytes_view_t bytes, IndexBase &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const IndexBase &object) { return generic_estimate(object); }

void encode(const IndexBase &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Ping &dest) { return 0; }

std::size_t estimate(const Ping &object) { return 0; }

void encode(const Ping &, utils::bytes_view_t) {}

int decode(utils::bytes_view_t bytes, Request &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Request &object) { return generic_estimate(object); }

void encode(const Request &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

int decode(utils::bytes_view_t bytes, Response &dest) { return generic_decode(bytes, dest); }

std::size_t estimate(const Response &object) { return generic_estimate(object); }

void encode(const Response &object, utils::bytes_view_t dest) { encode_unsafe(object, dest); }

utils::bytes_t encode(const Hello &object, std::size_t prefix) {
    using coder_t = pp::message_coder<Hello>;
    using skipper_t = pp::skipper<coder_t>;
    auto size = skipper_t::encode_skip(object);
    auto storage = utils::bytes_t(size + prefix);
    auto bytes = pp::bytes((std::byte *)storage.data() + prefix, size);
    coder_t::template encode<pp::unsafe_mode>(object, bytes);
    return storage;
}

Index convert(IndexUpdate &&update) noexcept {
    using namespace pp;
    auto index = Index();
    index["folder"_f] = std::move(update["folder"_f]);
    index["files"_f] = std::move(update["files"_f]);
    return index;
}

} // namespace syncspirit::proto
