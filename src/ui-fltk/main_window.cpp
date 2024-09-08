#include "main_window.h"

#include "log_panel.h"
#include "tree_view.h"
#include "toolbar.h"

#include <FL/Fl_Box.H>
#include <FL/Fl_Tile.H>

using namespace syncspirit::fltk;

main_window_t::main_window_t(app_supervisor_t &supervisor_)
    : parent_t(700, 480, "syncspirit-fltk"), supervisor{supervisor_} {

    auto container = new Fl_Tile(0, 0, w(), h());
    container->begin();

    auto resizeable_area = new Fl_Box(w() * 1. / 6, h() * 1. / 6, w() * 4. / 6, h() * 3. / 6);

    auto content_w = w() / 2;
    auto content_h = h() * 2 / 3;

    auto content_l = new Fl_Group(0, 0, content_w, content_h);
    content_l->box(FL_ENGRAVED_BOX);
    content_l->begin();
    auto toolbar = new toolbar_t(supervisor, 0, 0, content_w, 0);
    toolbar->end();
    auto toolbar_h = toolbar->h();
    auto tree = new tree_view_t(supervisor, 0, toolbar_h, content_w, content_h - toolbar_h);
    content_l->end();

    supervisor.replace_content([&](content_t *) -> content_t * {
        struct my_box_t : contentable_t<Fl_Box> {
            using parent_t = contentable_t<Fl_Box>;
            using parent_t::parent_t;
            void refresh() override {}
        };

        auto box = new my_box_t(content_w, 0, content_w, content_h, "to be loaded...");
        box->box(FL_ENGRAVED_BOX);
        return box;
    });

    log_panel = new log_panel_t(supervisor, 0, 0, w(), h() * 1 / 3);
    log_panel->position(0, content_l->h());
    log_panel->box(FL_FLAT_BOX);

    container->end();
    container->resizable(resizeable_area);

    end();

    resizable(this);
}

main_window_t::~main_window_t() { delete log_panel; }
