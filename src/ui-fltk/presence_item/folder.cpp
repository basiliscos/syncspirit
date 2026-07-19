// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "folder.h"
#include "symbols.h"
#include "table_widget/checkbox.h"
#include "table_widget/choice.h"
#include "table_widget/input.h"
#include "table_widget/label.h"
#include "content/folder_widget.h"
#include "presentation/folder_presence.h"
#include <FL/Fl_Tabs.H>
#include <FL/platform.H>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::presence_item;

static constexpr int padding = 2;

namespace {

struct widget_t : content::folder_widget_t {
    using parent_t = content::folder_widget_t;
    using parent_t::parent_t;
};

} // namespace

folder_t::folder_t(presentation::folder_presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(presence, supervisor, tree) {
    update_label();
    populate_dummy_child();
}

folder_t::~folder_t() {
    if (auto p = parent(); p) {
        auto index = p->find_child(this);
        if (index >= 0) {
            if (is_selected()) {
                select_other();
            }
            p->deparent(index);
            if (auto ti = dynamic_cast<tree_item_t *>(p); ti) {
                ti->update_label();
            }
        }
    }
}

bool folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using B = content::folder_widget_t::behavior_t;
        auto prev = content->get_widget();

        auto fp = static_cast<presentation::folder_presence_t *>(presence);
        auto &folder_info = fp->get_folder_info();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto is_local = folder_info.get_device() == folder_info.get_folder()->get_cluster()->get_device();
        auto b = is_local ? B::local : B::remote;
        auto widget = new widget_t(*this, b, x, y, w, h);
        widget->make_tabs(folder_info.get_folder(), &folder_info);
        return widget;
    });
    return true;
}
