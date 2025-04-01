// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"

#include "model/folder.h"

#include <cassert>
#include <boost/container/small_vector.hpp>
#include <boost/nowide/convert.hpp>
#include <filesystem>
#include <unordered_map>

using namespace syncspirit;
using namespace syncspirit::presentation;

namespace bfs = std::filesystem;

static constexpr size_t FILES_ON_STACK = 16;

using presensce_files_t = boost::container::small_vector<model::file_info_t *, FILES_ON_STACK>;
using path_t = std::vector<std::string>;

entity_t::entity_t(entity_t *parent_) : parent{parent_} {}

entity_t::~entity_t() {}

std::string_view entity_t::get_name() const { return name; }

void entity_t::remove_presense(presence_t &item) {
    auto predicate = [&item](const record_t &record) { return record.presence == &item; };
    auto it = std::find_if(records.begin(), records.end(), predicate);
    assert(it != records.end());
    records.erase(it);
}

presence_t *entity_t::get_presense_raw(model::device_t &device) {
    presence_t *fallback = nullptr;
    for (auto &record : records) {
        auto d = record.device.get();
        if (d == &device) {
            return record.presence;
        } else if (!d) {
            fallback = record.presence;
        }
    }
    return fallback;
}

void entity_t::on_update() noexcept { notify_update(); }
void entity_t::on_delete() noexcept {}

static entity_ptr_t make_file(entity_t *parent, model::file_info_t &f, const path_t &path) {
    auto all_files = presensce_files_t();
    auto name = f.get_name();
    auto &fi_map = f.get_folder_info()->get_folder()->get_folder_infos();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        auto file = files_map.by_name(name);
        if (file) {
            all_files.emplace_back(file.get());
        }
    }

    auto &own_name = path.back();

    return {};
}

entity_ptr_t make(model::folder_t &folder) {
    using visited_files_t = std::unordered_map<std::string_view, path_t>;
    auto &fi_map = folder.get_folder_infos();
    auto visited_files = visited_files_t();
    for (auto &it_fi : fi_map) {
        auto &folder_info = it_fi.item;
        auto &files_map = folder_info->get_file_infos();
        for (auto &it_file : files_map) {
            auto &file = it_file.item;
            auto name = file->get_name();
            if (visited_files.count(name)) {
                continue;
            }
            auto file_path = bfs::path(boost::nowide::widen(name));
            auto string_path = path_t();
            for (auto it = file_path.begin(); it != file_path.end(); ++it) {
                auto name = boost::nowide::narrow(it->wstring());
                string_path.emplace_back(std::move(name));
            }

            visited_files.emplace(name, std::move(string_path));
        }
    }
    return {};
}
