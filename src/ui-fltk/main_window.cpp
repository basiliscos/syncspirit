// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2026 Ivan Baidakou

#include "main_window.h"

#include "app.h"
#include "log_panel.h"
#include "tree_view.h"
#include "tree_item.h"
#include "menu.h"
#include "constants.h"
#include "utils/path_view.hpp"
#include "utils/format.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Tile.H>
#include <FL/platform.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_PNG_Image.H>
#include <fmt/format.h>

using namespace syncspirit;
using namespace syncspirit::fltk;

static auto app_name = fmt::format("syncspirit-fltk {}", constants::client_version);

main_window_t::main_window_t(app_supervisor_t &supervisor_, int w_, int h_)
    : parent_t(w_, h_, app_name.data()), supervisor{&supervisor_} {
    supervisor->set_main_window(this);

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto icon_path = supervisor->resolve_resource(allocator, "icons/syncspirit-fltk.png");
    if (!icon_path.empty()) {
        image_icon.reset(new Fl_PNG_Image(icon_path.get_full_name().data()));
        if (image_icon->w() && image_icon->h()) {
            icon(image_icon.get());
            Fl_Window::default_icon(static_cast<Fl_RGB_Image *>(image_icon.get()));
        } else {
            auto &log = supervisor->get_logger();
            LOG_WARN(log, "failed to load app icon at {}", icon_path);
        }
    }
    auto top_contaner = new Fl_Group(0, 0, w(), h());

    top_contaner->begin();

    menu = new menu_t(supervisor_, 0, 0, w(), 25);
    auto hh = h() - menu->h();
    auto container = new Fl_Tile(0, menu->h(), w(), hh);
    container->box(FL_FLAT_BOX);
    auto &cfg = supervisor->get_app_config().fltk_config;
    auto left_share = std::min(std::max(0.1, cfg.left_panel_share), 0.9);
    auto bottom_share = std::min(std::max(0.1, cfg.bottom_panel_share), 0.9);
    container->begin();

    auto resizable_area = new Fl_Box(w() * 0.1, container->y() + hh * 0.15, w() * 0.7, hh * 0.7);

    auto left_w = static_cast<int>(container->w() * left_share);
    auto right_w = container->w() - left_w;
    auto top_h = static_cast<int>(hh * (1 - bottom_share));
    content_left = new Fl_Group(0, menu->h(), left_w, top_h);
    content_left->box(FL_FLAT_BOX);
    content_left->begin();

    tree = new tree_view_t(*supervisor, 0, container->y(), left_w, top_h);
    content_left->end();
    content_left->resizable(tree);

    supervisor->replace_content([&](content_t *) -> content_t * {
        struct my_box_t : contentable_t<Fl_Box> {
            using parent_t = contentable_t<Fl_Box>;
            using parent_t::parent_t;
            void refresh() override {}
        };

        auto box = new my_box_t(tree->x() + tree->w(), tree->y(), right_w, top_h, "...");
        box->box(FL_FLAT_BOX);
        return box;
    });

    auto log_panel_h = h() - (content_left->h() + menu->h());
    log_panel = new log_panel_t(*supervisor, 0, 0, w(), log_panel_h);
    log_panel->position(0, content_left->h() + content_left->y());
    log_panel->box(FL_FLAT_BOX);

    container->end();
    container->resizable(resizable_area);

    top_contaner->end();
    end();

    top_contaner->resizable(container);

    resizable(this);
    deactivate();

    tray.init(*supervisor);
}

main_window_t::~main_window_t() {
    tray.enable(false);
    // one of the child d-tors use `main_window` object (indirectly),
    // hence it should be alive a little bit before children deletion
    clear();
}

void main_window_t::on_shutdown() {
    auto &cfg = supervisor->get_app_config().fltk_config;
    cfg.main_window_height = h();
    cfg.main_window_width = w();
    cfg.left_panel_share = (content_left->w() + 0.) / w();
    cfg.bottom_panel_share = (log_panel->h() + 0.) / h();

    for (auto *item = tree->first(); item; item = tree->next(item)) {
        if (auto tree_item = dynamic_cast<tree_item_t *>(item); tree_item) {
            if (auto augmentation = tree_item->get_proxy(); augmentation) {
                auto aug = dynamic_cast<augmentation_base_t *>(augmentation.get());
                if (aug) {
                    aug->release_owner();
                }
            }
        }
    }
}

int main_window_t::handle(int e) {
    if (e == FL_KEYDOWN && Fl::event_key() == FL_Escape) {
        auto &log = supervisor->get_logger();
        if (tray.is_enabled() && supervisor->get_app_config().fltk_config.hide_to_tray) {
            LOG_DEBUG(log, "hiding main window");
            hide();
        } else {
            LOG_INFO(log, "triggering quit");
            supervisor->do_shutdown();
        }
        return 1;
    }
    return parent_t::handle(e);
}

void main_window_t::set_splash_text(std::string text) {
    log_panel->set_splash_text(std::move(text));
    Fl::flush();
}

void main_window_t::show_tray_icon(bool value) noexcept { tray.enable(value); }

void main_window_t::on_loading_done() {
    auto &cfg = supervisor->get_app_config().fltk_config;
    if (cfg.display_tray_icon) {
        tray.enable(true);
    }
    activate();
}

void main_window_t::detach_supervisor() {
    tray.enable(false);
    clear();
    supervisor = nullptr;
}

void main_window_t::on_frame_render() noexcept { tray.on_frame_render(); }

void main_window_t::on_local_state_update() noexcept {
    menu->on_local_state_update();
    tray.on_local_state_update();
}

app_supervisor_t *main_window_t::get_supervisor() { return supervisor; }

const Fl_RGB_Image *main_window_t::get_icon() const noexcept { return image_icon.get(); }
