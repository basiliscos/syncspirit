// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file_entity.h"
#include "local_file_presence.h"
#include "peer_file_presence.h"
#include "missing_file_presence.h"
#include "model/cluster.h"
#include "model/folder.h"
#include "model/folder_info.h"

#include <boost/container/small_vector.hpp>

using namespace syncspirit;
using namespace syncspirit::presentation;

static constexpr size_t FILES_ON_STACK = 16;

using presence_files_t = boost::container::small_vector<model::file_info_t *, FILES_ON_STACK>;

file_entity_t::file_entity_t(model::file_info_t &sample_file, std::string_view own_name_) : entity_t(nullptr) {
    this->name = own_name_;
    auto presence_files = presence_files_t();
    auto path = sample_file.get_name();
    auto &fi_map = sample_file.get_folder_info()->get_folder()->get_folder_infos();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        auto file = files_map.by_name(path);
        if (file) {
            presence_files.emplace_back(file.get());
        }
    }

    records.emplace_back(record_t{{}, new missing_file_presence_t(*this)});

    for (auto &it : presence_files) {
        on_insert(*it);
    }
}

file_entity_t::~file_entity_t() {
    for (auto &r : records) {
        auto file = static_cast<file_presence_t *>(r.presence);
        if (file->get_presence_feautres() & file_presence_t::features_t::missing) {
            delete file;
        }
    }
    records.clear();
}

void file_entity_t::on_insert(model::file_info_t &file_info) {
    auto fi = file_info.get_folder_info();
    auto device = fi->get_device();
    auto local = fi->get_folder()->get_cluster()->get_device() == device;
    auto child = [&]() -> file_presence_t * {
        if (local) {
            return new local_file_presence_t(*this, file_info);
        } else {
            return new peer_file_presence_t(*this, file_info);
        }
    }();
    records.emplace_back(record_t{device, child});
}
