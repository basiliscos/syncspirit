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

            auto invisible = new Fl_Box(xx, yy, w - (xx - group->x() + padding * 2), hh);
            invisible->hide();
            group->resizable(invisible);
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
    using parent_t::parent_t;

    table_t(tree_item_t &container_, const folder_description_t &description, int x, int y, int w, int h)
        : parent_t(container_, description, x, y, w, h) {

        entries_cell = new static_string_provider_t(std::to_string(entries));
        max_sequence_cell = new static_string_provider_t(std::to_string(max_sequence));
        scan_start_cell = new static_string_provider_t();
        scan_finish_cell = new static_string_provider_t();

        auto data = table_rows_t();
        data.push_back({"", make_title(*this, "accepting pending folder")});
        data.push_back({"path", make_path(*this, false)});
        data.push_back({"id", make_id(*this, false)});
        data.push_back({"label", make_label(*this)});
        data.push_back({"type", make_folder_type(*this)});
        data.push_back({"pull order", make_pull_order(*this)});
        data.push_back({"index", make_index(*this, true)});
        data.push_back({"read only", make_read_only(*this)});
        data.push_back({"rescan interval", make_rescan_interval(*this)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this)});
        data.push_back({"ignore delete", make_ignore_delete(*this)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this)});
        data.push_back({"paused", make_paused(*this)});
        data.push_back({"shared_with", make_shared_with(*this, shared_with->begin()->item, true)});
        data.push_back({"", notice = make_notice(*this)});
        data.push_back({"actions", make_actions(*this)});

        initially_shared_with = *shared_with;
        initially_non_shared_with = *non_shared_with;
        assign_rows(std::move(data));

        refresh();
    }

    void refresh() override {
        serialiazation_context_t ctx;
        folder_data.serialize(ctx.folder);

        auto copy_data = ctx.folder.SerializeAsString();
        error = {};
        auto valid = store(&ctx);

        // clang-format off
        auto is_same = (copy_data == ctx.folder.SerializeAsString())
                    && (initially_shared_with == ctx.shared_with);
        // clang-format on

        if (valid) {
            if (ctx.folder.path().empty()) {
                error = "path should be defined";
            } else {
                auto path = bfs::path(ctx.folder.path());
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

        auto &path = supervisor.get_app_config().default_location;
        folder_data.set_path(path);
        folder_data.set_rescan_interval(3600u);

        auto description = table_t::folder_description_t{std::move(folder_data),    0,           folder.get_index(),
                                                         folder.get_max_sequence(), shared_with, non_shared_with};
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, description, x, y, w, h);
    });
    return true;
}
