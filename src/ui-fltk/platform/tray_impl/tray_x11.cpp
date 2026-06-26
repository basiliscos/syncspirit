// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "syncspirit-fltk-config.h"

#if defined(SYNCSPIRIT_FLTK_X11)

#include "tray_x11.h"

#include "app_supervisor.h"
#include "main_window.h"

#include <cstdlib>
#include <cstdio>
#include <FL/x.H>
#include <FL/Fl_SVG_Image.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Button.H>

using namespace syncspirit::fltk;

static void x11_event_poller(void *data) {
    auto dpy = fl_display;
    auto tray_widget = reinterpret_cast<tray_x11_t *>(data);
    if (!dpy || !tray_widget) {
        return;
    }

    XEvent ev;
    auto tray_win = fl_xid(tray_widget->tray_window);
    while (XCheckWindowEvent(dpy, tray_win, ExposureMask | ButtonPressMask, &ev)) {
        if (ev.type == Expose) {
            GC gc = DefaultGC(dpy, DefaultScreen(dpy));
            XSetForeground(dpy, gc, WhitePixel(dpy, DefaultScreen(dpy)));
            XFillRectangle(dpy, tray_win, gc, 4, 4, 16, 16);
            XSetForeground(dpy, gc, WhitePixel(dpy, DefaultScreen(dpy)));
            XDrawString(dpy, tray_win, gc, 9, 16, "X", 1);
        }
    }
    Fl::repeat_timeout(0.05, x11_event_poller, data);
}

static void cb_quit(Fl_Widget *w, void *data) {
    auto tray_widget = reinterpret_cast<tray_x11_t *>(data);
    auto &sup = tray_widget->sup;
    sup.get_logger()->info("exiting via menu");
    sup.do_shutdown();
}

static void cb_mouse_click(Fl_Widget *w, void *data) {
    auto tray_widget = reinterpret_cast<tray_x11_t *>(data);
    auto button = Fl::event_button();
    if (button == FL_RIGHT_MOUSE) {
        auto xx = Fl::event_x();
        auto yy = Fl::event_x();
        auto picked = tray_widget->menu_items.data()->popup(xx, yy);
        if (picked) {
            picked->do_callback(w, data);
        }
    }
    if (button == FL_LEFT_MOUSE) {
        auto main_window = tray_widget->sup.get_main_window();
        if (main_window) {
            if (main_window->shown()) {
                main_window->hide();
            } else {
                main_window->show();
            }
        }
    }
}

static Fl_Menu_Item tray_menu_items[] = {{"Quit Application", 0, cb_quit, nullptr, 0, 0, 0, 14, 0}, {nullptr}};

tray_x11_t *tray_x11_t::init(app_supervisor_t &sup) noexcept {
    Display *display = fl_display;
    if (!std::getenv("DISPLAY") || !display) {
        return nullptr;
    }

    auto screen = DefaultScreen(display);
    char sel_name[64];
    std::snprintf(sel_name, sizeof(sel_name), "_NET_SYSTEM_TRAY_S%d", screen);
    auto selection_atom = XInternAtom(display, sel_name, False);
    if (selection_atom == None) {
        return {};
    }

    auto owner = XGetSelectionOwner(display, selection_atom);
    if (owner == None) {
        return {};
    }

    auto opcode_atom = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", False);
    if (!opcode_atom) {
        return {};
    }
    auto xembed_atom = XInternAtom(display, "_XEMBED", False);
    if (!xembed_atom) {
        return {};
    }
    auto xembed_info_atom = XInternAtom(display, "_XEMBED_INFO", False);
    if (!xembed_atom) {
        return {};
    }

    auto tray_window = new tray_window_t(24, 24);

    const char *star_svg = "<svg height='24' width='24' viewBox='0 0 24 24'>"
                           "  <polygon points='12,2 15,9 22,9 17,14 19,21 12,17 5,21 7,14 2,9 9,9' fill='#ffcc00' "
                           "stroke='#d4af37' stroke-width='1'/>"
                           "</svg>";

    Fl_SVG_Image *svg_icon = new Fl_SVG_Image(nullptr, star_svg);
    auto icon_box = new Fl_Button(0, 0, 24, 24);
    icon_box->image(svg_icon);
    icon_box->box(FL_FLAT_BOX);

    tray_window->end();
    tray_window->show();

    Fl::flush();

    auto w = fl_xid(tray_window);
    if (!w) {
        delete tray_window;
        return {};
    }

    // TODO: add timeout;
    while (!tray_window->shown()) {
        Fl::check();
    }

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    XChangeWindowAttributes(display, w, CWOverrideRedirect, &attr);
    XSelectInput(display, w, ExposureMask | ButtonPressMask | ButtonReleaseMask);

    auto window =
        new tray_x11_t(selection_atom, opcode_atom, xembed_atom, xembed_info_atom, tray_window, owner, w, sup);

    icon_box->callback(cb_mouse_click, window);
    icon_box->when(FL_WHEN_RELEASE | FL_WHEN_NOT_CHANGED);

    Fl::add_timeout(0.05, x11_event_poller, window);

    return window;
}

tray_x11_t::tray_x11_t(Atom selection_atom_, Atom opcode_atom_, Atom xembed_atom_, Atom xembed_info_atom_,
                       tray_window_t *tray_window_, Window owner_, Window w_, app_supervisor_t &sup_)
    : selection_atom{selection_atom_}, opcode_atom{opcode_atom_}, xembed_atom{xembed_atom_},
      xembed_info_atom{xembed_info_atom_}, tray_window{tray_window_}, sup{sup_}

{
    tray_window->tray = this;
    tray_window->border(0);

    XClientMessageEvent ev{};
    ev.type = ClientMessage;
    ev.window = owner_;
    ev.message_type = opcode_atom;
    ev.format = 32;
    ev.data.l[0] = CurrentTime;
    ev.data.l[1] = 0; // _NET_SYSTEM_TRAY_REQUEST_DOCK
    ev.data.l[2] = static_cast<long>(w_);
    ev.data.l[3] = 0;
    ev.data.l[4] = 0;

    XSendEvent(fl_display, owner_, False, NoEventMask, reinterpret_cast<XEvent *>(&ev));

    XSync(fl_display, False);

    menu_items.push_back({"Quit", 0, cb_quit, nullptr, 0, 0, 0, 14, 0});
    menu_items.push_back({nullptr});
}

#endif
