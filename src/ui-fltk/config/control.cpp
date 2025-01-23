// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "control.h"

#include "table.h"
#include "config/utils.h"
#include "../tree_item/settings.h"
#include <FL/Fl_Button.H>
#include <fstream>

namespace syncspirit::fltk::config {

static constexpr int PADDING = 5;

control_t::control_t(tree_item_t &tree_item_, int x, int y, int w, int h)
    : parent_t(x, y, w, h, "global app settings"), tree_item{tree_item_} {

    auto &sup = tree_item_.supervisor;
    auto config_path = sup.get_config_path();
    auto defaults_opt = syncspirit::config::generate_config(config_path);
    if (!defaults_opt) {
        auto ec = defaults_opt.assume_error();
        sup.get_logger()->error("cannot generate default config at {}: {}", config_path.string(), ec.message());
    } else {
        default_cfg = std::move(defaults_opt.assume_value());
        categories = reflect(sup.get_app_config(), default_cfg);
    }

    auto bottom_h = 40;
    auto table = new table_t(categories, x + PADDING, y + PADDING, w - PADDING * 2, h - (bottom_h + PADDING * 3));
    table->callback([](auto, void *data) { reinterpret_cast<control_t *>(data)->on_setting_modify(); }, this);

    auto bottom_group = new Fl_Group(x + PADDING, table->y() + table->h() + PADDING, w - PADDING * 2, bottom_h);
    bottom_group->begin();
    auto common_y = bottom_group->y() + PADDING;
    auto common_w = bottom_group->w() / 2 - PADDING;
    auto common_h = bottom_group->h() - PADDING * 2;
    auto save = new Fl_Button(bottom_group->x(), common_y, common_w, common_h, "save");
    save->callback([](Fl_Widget *, void *data) { reinterpret_cast<control_t *>(data)->on_save(); }, this);

    auto reset = new Fl_Button(save->x() + common_w + PADDING, common_y, common_w, common_h, "defaults");
    reset->callback([](Fl_Widget *, void *data) { reinterpret_cast<control_t *>(data)->on_reset(); }, this);

    bottom_group->end();

    resizable(table);
    box(FL_FLAT_BOX);
}

void control_t::on_reset() {
    for (auto &c : categories) {
        for (auto &p : c->get_properties()) {
            p->reset();
        }
    }
    auto table = static_cast<table_t *>(child(0));
    table->reload_values();
}

void control_t::on_setting_modify() {
    bool modified = false;
    for (size_t i = 0; i < categories.size() && !modified; ++i) {
        auto &c = categories[i];
        for (auto &p : c->get_properties()) {
            if (!p->same_as_initial()) {
                modified = true;
                break;
            }
        }
    }
    static_cast<tree_item::settings_t &>(tree_item).update_label(modified);
}

void control_t::on_save() {
    auto &sup = tree_item.supervisor;
    auto &log = sup.get_logger();
    if (!is_valid(categories)) {
        log->error("config has error, saving is not possible");
        return;
    }

    auto cfg = reflect(categories, default_cfg);
    cfg.config_path = sup.get_config_path();
    cfg.fltk_config = sup.get_app_config().fltk_config;

    sup.write_config(cfg);
}

} // namespace syncspirit::fltk::config
