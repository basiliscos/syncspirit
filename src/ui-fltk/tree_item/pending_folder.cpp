// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "pending_folder.h"
#include "pending_folders.h"
#include "../table_widget/checkbox.h"
#include "../content/folder_table.h"

#include <FL/Fl_Check_Button.H>
#include <spdlog/fmt/fmt.h>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

static constexpr int padding = 2;

namespace {

using folder_table_t = content::folder_table_t;

static auto make_actions(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto &container = static_cast<folder_table_t &>(this->container);

            auto yy = y + padding, ww = 100, hh = h - padding * 2;

            auto share = new Fl_Button(x + padding, yy, ww, hh, "share");
            share->deactivate();
            share->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_share(); }, &container);
            auto xx = share->x() + ww + padding * 2;
            container.share_button = share;

            auto reset = new Fl_Button(xx, yy, ww, hh, "reset");
            reset->deactivate();
            reset->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_reset(); }, &container);
            container.reset_button = reset;
            xx = reset->x() + ww + padding * 2;

            group->resizable(nullptr);
            group->end();
            widget = group;

            this->reset();
            return widget;
        }
    };

    return new widget_t(container);
}

struct table_t : content::folder_table_t {
    using parent_t = content::folder_table_t;

    table_t(tree_item_t &container_, model::folder_info_ptr_t fi_, model::folder_ptr_t folder_, int x, int y, int w,
            int h)
        : parent_t(container_, *fi_, x, y, w, h), fi{fi_}, folder{folder_}, existing{false} {

        entries_cell = new static_string_provider_t();
        max_sequence_cell = new static_string_provider_t();
        scan_start_cell = new static_string_provider_t();
        scan_finish_cell = new static_string_provider_t();

        auto folder_id = folder_->get_id();
        existing = (bool)container_.supervisor.get_cluster()->get_folders().by_id(folder_id);

        auto data = table_rows_t();
        data.push_back({"", make_title(*this, "accepting pending folder")});
        data.push_back({"path", make_path(*this, existing)});
        data.push_back({"id", make_id(*this, existing)});
        data.push_back({"label", make_label(*this, existing)});
        data.push_back({"type", make_folder_type(*this, existing)});
        data.push_back({"pull order", make_pull_order(*this, existing)});
        data.push_back({"index", make_index(*this, true)});
        data.push_back({"rescan interval", make_rescan_interval(*this, existing)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this, existing)});
        data.push_back({"ignore delete", make_ignore_delete(*this, existing)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this, existing)});
        data.push_back({"scheduled", make_scheduled(*this, existing)});
        data.push_back({"paused", make_paused(*this, existing)});
        data.push_back({"shared_with", make_shared_with(*this, fi->get_device(), true)});
        data.push_back({"", notice = make_notice(*this)});
        data.push_back({"actions", make_actions(*this)});

        initially_shared_with = *shared_with;
        initially_non_shared_with = *non_shared_with;
        assign_rows(std::move(data));

        refresh();
    }

    void refresh() override {
        serialization_context_t ctx;
        folder->serialize(ctx.folder);

        auto copy_data = db::encode(ctx.folder);
        error = {};
        auto valid = store(&ctx);

        if (valid) {
            auto db_path = db::get_path(ctx.folder);
            if (db_path.empty()) {
                error = "path should be defined";
            } else {
                auto path = bfs::path(boost::nowide::widen(db_path));
                auto ec = sys::error_code{};
                if (bfs::exists(path, ec)) {
                    if (!bfs::is_empty(path, ec)) {
                        error = "referred directory should be empty";
                    }
                }
            }
        }

        if (valid && error.empty()) {
            share_button->activate();
        } else {
            share_button->deactivate();
        }

        notice->reset();
        redraw();
    }

    model::folder_info_ptr_t fi;
    model::folder_ptr_t folder;
    bool existing;
};

} // namespace

pending_folder_t::pending_folder_t(model::pending_folder_t &folder_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), folder{folder_} {

    auto l = fmt::format("{} ({})", folder.get_label(), folder.get_id());
    label(l.c_str());
}

bool pending_folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        auto cluster = supervisor.get_cluster();
        auto &sequencer = supervisor.get_sequencer();
        auto &peer = static_cast<pending_folders_t *>(parent())->peer;
        auto &path = supervisor.get_app_config().default_location;

        auto db = db::PendingFolder();
        folder.serialize(db);

        auto &db_folder = db::get_folder(db);
        db::set_path(db_folder, path.string());
        db::set_rescan_interval(db_folder, 3600);

        auto folder = model::folder_t::create(sequencer.next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        auto db_folder_info = db::FolderInfo();
        db::set_index_id(db_folder_info, sequencer.next_uint64());
        auto fi = model::folder_info_t::create(sequencer.next_uuid(), db_folder_info, &peer, folder).value();
        folder->get_folder_infos().put(fi);

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, std::move(fi), std::move(folder), x, y, w, h);
    });
    return true;
}
