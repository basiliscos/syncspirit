// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "pending_folder.h"
#include "pending_folders.h"
#include "../table_widget/checkbox.h"
#include "../content/folder_table.h"

#include <FL/Fl_Check_Button.H>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

pending_folder_t::pending_folder_t(model::pending_folder_t &folder_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), folder{folder_} {

    auto l = fmt::format("{} ({})", folder.get_label(), folder.get_id());
    label(l.c_str());
}

bool pending_folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using table_t = content::folder_table_t;
        auto prev = content->get_widget();
        auto folder_data = model::folder_data_t(folder);
        auto shared_with = table_t::shared_devices_t{new model::devices_map_t{}};
        auto non_shared_with = table_t::shared_devices_t{new model::devices_map_t{}};

        auto cluster = supervisor.get_cluster();
        auto &devices = cluster->get_devices();
        auto &self = *cluster->get_device();
        auto &peer = static_cast<pending_folders_t *>(parent())->peer;
        for (auto &it : devices) {
            auto &device = *it.item;
            if ((&device != &self) && (&device != &peer)) {
                non_shared_with->put(it.item);
            }
        }

        shared_with->put(&peer);

        auto description = table_t::folder_description_t{std::move(folder_data),    0,           folder.get_index(),
                                                         folder.get_max_sequence(), shared_with, non_shared_with};
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, description, table_t::mode_t::share, x, y, w, h);
    });
    return true;
}
