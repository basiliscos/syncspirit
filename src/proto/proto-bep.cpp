// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "proto-bep.h"

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

void Vector::add_counter(proto::Counter value) noexcept {
    using namespace pp;
    (*impl)["_counters"_f].emplace_back(std::move(value.expose()));
}

void FileInfo::add_block(proto::BlockInfo value) noexcept {
   using namespace pp;
   (*impl)["_blocks"_f].emplace_back(std::move(value.expose()));
}

void ClusterConfig::add_folder(proto::Folder value) noexcept {
    using namespace pp;
    (*impl)["_folders"_f].emplace_back(std::move(value.expose()));
}


void IndexBase::add_file(proto::FileInfo file) noexcept {
    using namespace pp;
    (*impl)["_files"_f].push_back(std::move(file.expose()));
}

}

}

