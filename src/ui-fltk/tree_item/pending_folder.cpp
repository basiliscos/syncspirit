// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "pending_folder.h"
#include "pending_folders.h"
#include "content/folder_widget.h"
#include "proto/proto-helpers-db.h"

#include <FL/Fl_Check_Button.H>
#include <spdlog/fmt/fmt.h>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

static constexpr int padding = 2;

namespace {

struct widget_t final : content::folder_widget_t {
    using parent_t = content::folder_widget_t;
    using parent_t::parent_t;
};
} // namespace

pending_folder_t::pending_folder_t(model::pending_folder_t &folder_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), folder{folder_} {

    auto l = fmt::format("{} ({})", folder.get_label(), folder.get_id());
    label(l.c_str());
}

bool pending_folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using B = content::folder_widget_t::behavior_t;
        auto prev = content->get_widget();
        auto cluster = supervisor.get_cluster();
        auto &sequencer = supervisor.get_sequencer();
        auto &peer = static_cast<pending_folders_t *>(parent())->peer;
        auto &path = supervisor.get_app_config().default_location;

        auto db = db::PendingFolder();
        folder.serialize(db);

        auto &db_folder = db::get_folder(db);
        db::set_path(db_folder, path.get_full_name());
        db::set_rescan_interval(db_folder, 3600);
        db::set_watched(db_folder, true);

        auto folder = model::folder_t::create(sequencer.next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        auto db_folder_info = db::FolderInfo();
        db::set_index_id(db_folder_info, sequencer.next_uint64());
        auto fi = model::folder_info_t::create(sequencer.next_uuid(), db_folder_info, &peer, folder).value();
        folder->get_folder_infos().put(fi);

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto widget = new widget_t(*this, B::candiate, x, y, w, h);
        widget->make_tabs(std::move(folder), std::move(fi));
        return widget;
    });
    return true;
}
