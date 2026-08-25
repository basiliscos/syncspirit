// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "folders.h"
#include "presence_item/folder.h"
#include "presentation/folder_presence.h"
#include "utils/base32.h"
#include "proto/proto-helpers-db.h"
#include "content/folder_widget.h"
#include <FL/Fl_Button.H>

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;
using namespace syncspirit::fltk::presence_item;

static constexpr int padding = 2;

namespace {

struct widget_t final : content::folder_widget_t {
    using parent_t = content::folder_widget_t;
    using parent_t::parent_t;
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
        using B = content::folder_widget_t::behavior_t;
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
        db::set_path(db_folder, path.get_full_name());
        db::set_id(db_folder, id);
        db::set_folder_type(db_folder, db::FolderType::send_and_receive);
        db::set_watched(db_folder, true);

        auto matcher = db::FileMatcher();
        db::set_mode(matcher, db::FileMatch::accept);
        db::set_pattern(matcher, std::string(".*"));
        db::set_ignore_case(matcher, true);
        db::add_file_matcher(db_folder, std::move(matcher));

        auto folder = model::folder_t::create(sequencer.next_uuid(), db_folder).value();
        folder->assign_cluster(cluster);

        auto db_folder_info = db::FolderInfo();
        db::set_index_id(db_folder_info, sequencer.next_uint64());
        auto fi = model::folder_info_t::create(sequencer.next_uuid(), db_folder_info, &self, folder).value();

        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto widget = new widget_t(*this, B::edit_new, x, y, w, h);
        widget->make_tabs(std::move(folder), std::move(fi));
        return widget;
    });
    return true;
}
