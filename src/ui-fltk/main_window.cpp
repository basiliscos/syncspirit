// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "main_window.h"

#include "log_panel.h"
#include "tree_view.h"
#include "tree_item.h"
#include "toolbar.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Tile.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(app_supervisor_t &supervisor_, int w_, int h_)
    : parent_t(w_, h_, "syncspirit-fltk"), supervisor{supervisor_} {

    auto container = new Fl_Tile(0, 0, w(), h());
    auto &cfg = supervisor.get_app_config().fltk_config;
    auto left_share = std::min(std::max(0.1, cfg.left_panel_share), 0.9);
    auto bottom_share = std::min(std::max(0.1, cfg.bottom_panel_share), 0.9);
    container->begin();

    auto resizeable_area = new Fl_Box(w() * 0.1, h() * 0.1, w() * 0.9, h() * 0.8);

    auto left_w = static_cast<int>(w() * left_share);
    auto right_w = w() - left_w;
    auto top_h = static_cast<int>(h() * (1 - bottom_share));
    auto bottom_h = h() - top_h;
    content_left = new Fl_Group(0, 0, left_w, top_h);
    content_left->box(FL_ENGRAVED_BOX);
    content_left->begin();
    auto toolbar = new toolbar_t(supervisor, 0, 0, left_w, 0);
    toolbar->end();
    auto toolbar_h = toolbar->h();
    tree = new tree_view_t(supervisor, 0, toolbar_h, left_w, top_h - toolbar_h);
    content_left->end();
    content_left->resizable(tree);

    supervisor.replace_content([&](content_t *) -> content_t * {
        struct my_box_t : contentable_t<Fl_Box> {
            using parent_t = contentable_t<Fl_Box>;
            using parent_t::parent_t;
            void refresh() override {}
        };

        auto box = new my_box_t(left_w, 0, right_w, top_h, "...");
        box->box(FL_ENGRAVED_BOX);
        return box;
    });

    log_panel = new log_panel_t(supervisor, 0, 0, w(), bottom_h);
    log_panel->position(0, content_left->h());
    log_panel->box(FL_FLAT_BOX);

    container->end();
    container->resizable(resizeable_area);

    end();

    resizable(this);
    supervisor.set_main_window(this);
    deactivate();
}

void main_window_t::on_shutdown() {
    auto &cfg = supervisor.get_app_config().fltk_config;
    cfg.main_window_height = h();
    cfg.main_window_width = w();
    cfg.left_panel_share = (content_left->w() + 0.) / w();
    cfg.bottom_panel_share = (log_panel->h() + 0.) / h();

    for (auto *item = tree->first(); item; item = tree->next(item)) {
        if (auto tree_item = dynamic_cast<tree_item_t *>(item); tree_item) {
            if (auto augmentation = tree_item->get_proxy(); augmentation) {
                auto aug = dynamic_cast<augmentation_base_t *>(augmentation.get());
                if (aug) {
                    aug->release_onwer();
                }
            }
        }
    }
}

void main_window_t::set_splash_text(std::string text) {
    log_panel->set_splash_text(std::move(text));
    Fl::flush();
}

void main_window_t::on_loading_done() {
    log_panel->on_loading_done();
    activate();
}
