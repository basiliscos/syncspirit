// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-bep.h"

using namespace pp;

namespace syncspirit::proto {

namespace view {

proto::BlockInfo BlockInfo::clone() const noexcept {
    return proto::BlockInfo(*impl);
}

proto::FileInfo FileInfo::clone() const noexcept {
    return proto::FileInfo(*impl);
}

}

namespace changeable {

void Announce::add_addresses(std::string value) noexcept {
    (*impl)["_addresses"_f].emplace_back(std::move(value));
}

void Vector::add_counter(proto::Counter value) noexcept {
    (*impl)["_counters"_f].emplace_back(std::move(value.expose()));
}

Counter Vector::add_new_counter() noexcept {
    auto& counters = (*impl)["_counters"_f];
    counters.emplace_back(details::Counter());
    return Counter(&counters.back());
}

Vector FileInfo::mutable_version() noexcept {
    auto& f = (*impl)["_version"_f];
    if (!f.has_value()) {
        f = proto::Vector().expose();
    }
    return Vector(&f.value());
}

void FileInfo::add_block(proto::BlockInfo value) noexcept {
   (*impl)["_blocks"_f].emplace_back(std::move(value.expose()));
}

void ClusterConfig::add_folder(proto::Folder value) noexcept {
    (*impl)["_folders"_f].emplace_back(std::move(value.expose()));
}


void IndexBase::add_file(proto::FileInfo file) noexcept {
    (*impl)["_files"_f].push_back(std::move(file.expose()));
}

}

}

