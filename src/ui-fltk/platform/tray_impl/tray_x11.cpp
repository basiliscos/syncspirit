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
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Menu_Button.H>
#include <FL/Fl_Button.H>
#include "FL/Fl_Window.H"
#include <chrono>

namespace syncspirit::fltk {

static void cb_mouse_click(Fl_Widget *w, void *data) {
    auto tray_widget = reinterpret_cast<tray_x11_t *>(data);
    auto button = Fl::event_button();
    if (button == FL_RIGHT_MOUSE) {
        auto xx = Fl::event_x();
        auto yy = Fl::event_y();
        auto picked = tray_widget->menu_items.data()->popup(xx, yy);
        if (picked) {
            picked->do_callback(nullptr, data);
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

struct tray_window_t : Fl_Double_Window {
    using parent_t = Fl_Double_Window;
    tray_window_t() : parent_t(24, 24) {
        box(FL_NO_BOX);
        border(0);
        icon_box = new Fl_Button(0, 0, w(), h());
        icon_box->box(FL_NO_BOX);

        icon_box->when(FL_WHEN_RELEASE | FL_WHEN_NOT_CHANGED);
    }

    ~tray_window_t() {
        if (scaled) {
            delete scaled;
        }
    }

    void bind(tray_x11_t *tray_) noexcept {
        tray = tray_;
        icon_box->callback(cb_mouse_click, tray_);
    }

    void assing(const Fl_Image *image) noexcept {
        if (scaled) {
            delete scaled;
        }
        current = image;
        scaled = current->copy(w(), h());
        icon_box->image(scaled);
        redraw();
    }

    void resize(int X, int Y, int W, int H) override {
        if (w() != W || h() != H) {
            parent_t::resize(X, Y, W, H);
            if (scaled) {
                delete scaled;
            }
            icon_box->resize(0, 0, W, H);
            scaled = current->copy(W, H);
            icon_box->image(scaled);
        }
    }

    const Fl_Image *current{nullptr};
    Fl_Image *scaled{nullptr};
    Fl_Button *icon_box;
    tray_x11_t *tray{nullptr};
};

static void x11_event_poller(int, void *data) {
    auto tray_widget = reinterpret_cast<tray_x11_t *>(data);
    if (!tray_widget || !tray_widget->watching_display) {
        return;
    }

    auto dpy = tray_widget->watching_display;
    XEvent ev;
    auto tray_win = fl_xid(tray_widget->tray_window);
    while (XCheckWindowEvent(dpy, tray_win, ExposureMask | ButtonPressMask | StructureNotifyMask, &ev)) {
        if (ev.type == ConfigureNotify) {
            auto *c = &ev.xconfigure;
            int new_w = c->width;
            int new_h = c->height;
            tray_widget->tray_window->resize(0, 0, new_w, new_h);
        }
    }

    Fl::awake();
}

tray_x11_t *tray_x11_t::init(app_supervisor_t &sup) noexcept {
    using clock_t = std::chrono::high_resolution_clock;
    using tray_window_guard_t = std::unique_ptr<tray_window_t>;

    auto display = fl_display;
    if (!display) {
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
    if (!xembed_info_atom) {
        return {};
    }

    if (!sup.get_main_window()->get_icon()) {
        return {};
    }

    auto tray_window = new tray_window_t();
    auto guard = tray_window_guard_t(tray_window);
    tray_window->show();

    // Fl::flush();

    auto w = fl_xid(tray_window);
    if (!w) {
        return {};
    }
    unsigned long buffer[2] = {0, 1}; // [0] = Protocol Version, [1] = XEMBED_MAPPED flags
    XChangeProperty(display, w, xembed_info_atom, xembed_info_atom, 32, PropModeReplace, (unsigned char *)buffer, 2);

    XSetWindowAttributes attr;
    attr.override_redirect = True;
    XChangeWindowAttributes(display, w, CWOverrideRedirect, &attr);
    XSelectInput(display, w, ExposureMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);

    Display *watching_display = XOpenDisplay(nullptr);

    auto window = new tray_x11_t(watching_display, selection_atom, opcode_atom, xembed_atom, xembed_info_atom,
                                 guard.release(), owner, w, sup);

    return window;
}

tray_x11_t::tray_x11_t(Display *watching_display_, Atom selection_atom_, Atom opcode_atom_, Atom xembed_atom_,
                       Atom xembed_info_atom_, tray_window_t *tray_window_, Window owner_, Window w_,
                       app_supervisor_t &sup_)
    : tray_impl_t{sup_}, watching_display{watching_display_}, selection_atom{selection_atom_},
      opcode_atom{opcode_atom_}, xembed_atom{xembed_atom_}, xembed_info_atom{xembed_info_atom_}, owner{owner_},
      window{w_}, tray_window{tray_window_} {
    int xfd = ConnectionNumber(watching_display);
    Fl::add_fd(xfd, FL_READ, x11_event_poller, this);

    tray_window->bind(this);
    tray_window->assing(sup.get_main_window()->get_icon());

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
}

void tray_x11_t::set_default_icon() noexcept { tray_window->assing(sup.get_main_window()->get_icon()); }

void tray_x11_t::set_traffic_icon() noexcept { tray_window->assing(traffic_image); }

void tray_x11_t::set_offline_icon() noexcept { tray_window->assing(offline_image); }

tray_x11_t::~tray_x11_t() {
    XSync(watching_display, False);
    int xfd = ConnectionNumber(watching_display);
    Fl::remove_fd(xfd, FL_READ);

    XClientMessageEvent ev{};
    ev.type = ClientMessage;
    ev.window = owner;
    ev.message_type = opcode_atom;
    ev.format = 32;
    ev.data.l[0] = CurrentTime;
    ev.data.l[1] = 1; // _NET_SYSTEM_TRAY_REQUEST_UNDOCK
    ev.data.l[2] = static_cast<long>(window);
    ev.data.l[3] = 0;
    ev.data.l[4] = 0;

    XSendEvent(fl_display, owner, False, NoEventMask, reinterpret_cast<XEvent *>(&ev));

    int screen = DefaultScreen(fl_display);
    auto root = RootWindow(fl_display, screen);
    XReparentWindow(fl_display, window, root, 0, 0);
    XSync(fl_display, False);
    XCloseDisplay(watching_display);

    delete tray_window;
}

bool tray_x11_t::is_enabled() noexcept { return tray_window; }

} // namespace syncspirit::fltk

#endif
