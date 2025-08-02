// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "folders.h"
#include "presence_item/folder.h"
#include "../content/folder_table.h"
#include "../table_widget/label.h"
#include "utils/base32.h"
#include "presentation/folder_presence.h"
#include <algorithm>
#include <cctype>
#include <boost/nowide/convert.hpp>
#include <FL/Fl_Button.H>

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;
using namespace syncspirit::fltk::presence_item;

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

            auto apply = new Fl_Button(x + padding, yy, ww, hh, "create");
            apply->deactivate();
            apply->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_apply(); }, &container);
            container.apply_button = apply;
            int xx = apply->x() + ww + padding * 2;

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
        : parent_t(container_, *fi_, x, y, w, h), fi{fi_}, folder{folder_} {

        scan_start_cell = new static_string_provider_t();
        scan_finish_cell = new static_string_provider_t();

        auto data = table_rows_t();
        data.push_back({"", make_title(*this, "creating new folder")});
        data.push_back({"path", make_path(*this, false)});
        data.push_back({"id", make_id(*this, false)});
        data.push_back({"label", make_label(*this, false)});
        data.push_back({"type", make_folder_type(*this, false)});
        data.push_back({"pull order", make_pull_order(*this, false)});
        data.push_back({"index", make_index(*this, false)});
        data.push_back({"rescan interval", make_rescan_interval(*this, false)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this, false)});
        data.push_back({"ignore delete", make_ignore_delete(*this, false)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this)});
        data.push_back({"scheduled", make_scheduled(*this, false)});
        data.push_back({"paused", make_paused(*this, false)});
        data.push_back({"shared_with", make_shared_with(*this, {}, false)});
        data.push_back({"", notice = make_notice(*this)});
        data.push_back({"actions", make_actions(*this)});

        initially_shared_with = *shared_with;
        initially_non_shared_with = *non_shared_with;

        assign_rows(std::move(data));

        refresh();
    }

    void refresh() override {
        serialization_context_t ctx;
        description.get_folder()->serialize(ctx.folder);

        auto copy_data = db::encode(ctx.folder);
        error = {};
        auto valid = store(&ctx);

        // clang-format off
        auto is_same = (copy_data == db::encode(ctx.folder))
                    && (initially_shared_with == ctx.shared_with);
        // clang-format on
        if (!is_same) {
            if (valid) {
                apply_button->activate();
            }
            reset_button->activate();
        } else {
            apply_button->deactivate();
            reset_button->deactivate();
        }

        if (valid) {
            auto db_path = db::get_path(ctx.folder);
            if (db_path.empty()) {
                error = "path should be defined";
            }
        }

        if (valid && error.empty()) {
            apply_button->activate();
        } else {
            apply_button->deactivate();
        }

        notice->reset();
        redraw();
    }

    model::folder_info_ptr_t fi;
    model::folder_ptr_t folder;
};

} // namespace

folders_t::folders_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    supervisor.set_folders(this);
    update_label();
}

void folders_t::update_label() {
    auto l = fmt::format("folders ({})", children());
    this->label(l.data());
}

auto folders_t::add_folder(presentation::folder_entity_t &folder_entity) -> augmentation_ptr_t {
    auto augmentation = within_tree([&]() {
        auto self = supervisor.get_cluster()->get_device();
        auto presence = folder_entity.get_presence(self.get());
        auto folder_presence = static_cast<presentation::folder_presence_t *>(presence);
        auto item = new folder_t(*folder_presence, supervisor, tree());
        return insert_by_label(item)->get_proxy();
    });
    update_label();
    return augmentation;
}

void folders_t::remove_child(tree_item_t *child) {
    parent_t::remove_child(child);
    update_label();
}

void folders_t::select_folder(std::string_view folder_id) {
    auto t = tree();
    for (int i = 0; i < children(); ++i) {
        auto folder = static_cast<folder_t *>(child(i));
        auto &fp = static_cast<presentation::folder_presence_t &>(folder->get_presence());
        if (fp.get_folder_info().get_folder()->get_id() == folder_id) {
            while (auto selected = t->first_selected_item()) {
                t->deselect(selected);
            }
            t->select(folder, 1);
            t->redraw();
            break;
        }
    }
}

bool folders_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto cluster = supervisor.get_cluster();
        auto &self = *cluster->get_device();
        auto &sequencer = supervisor.get_sequencer();

        auto random_id = sequencer.next_uint64();
        auto random_id_ptr = reinterpret_cast<unsigned char *>(&random_id);
        auto sample_id = utils::base32::encode(utils::bytes_view_t(random_id_ptr, sizeof(random_id)));
        auto lower_caser = [](unsigned char c) { return std::tolower(c); };
        std::transform(sample_id.begin(), sample_id.end(), sample_id.begin(), lower_caser);
        auto sz = sample_id.size();
        auto id = sample_id.substr(0, sz / 2) + "-" + sample_id.substr(sz / 2 + 1);
        auto &path = supervisor.get_app_config().default_location;

        auto db_folder = db::Folder();
        db::set_rescan_interval(db_folder, 3600);
        db::set_path(db_folder, boost::nowide::narrow(path.wstring()));
        db::set_id(db_folder, id);
        auto folder = model::folder_t::create(sequencer.next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        auto db_folder_info = db::FolderInfo();
        db::set_index_id(db_folder_info, sequencer.next_uint64());
        auto fi = model::folder_info_t::create(sequencer.next_uuid(), db_folder_info, &self, folder).value();

        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, std::move(fi), std::move(folder), x, y, w, h);
    });
    return true;
}
