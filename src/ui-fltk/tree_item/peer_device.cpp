#include "peer_device.h"

#include "../qr_button.h"

#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

peer_device_t::peer_widget_t::peer_widget_t(peer_device_t &container_) : container{container_} {}
Fl_Widget *peer_device_t::peer_widget_t::get_widget() { return widget; }

struct checkbox_widget_t : peer_device_t::peer_widget_t {
    using parent_t = peer_widget_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto group = new Fl_Group(x, y, w, h);
        group->begin();
        group->box(FL_FLAT_BOX);
        auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;

        input = new Fl_Check_Button(x + padding, yy, ww, hh);

        group->end();
        widget = group;
        return widget;
    }

    mutable Fl_Check_Button *input;
};

static peer_device_t::peer_widget_ptr_t make_actions(peer_device_t &container) {
    struct widget_t final : peer_device_t::peer_widget_t {
        using parent_t = peer_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            auto reset = new Fl_Button(apply->x() + ww + padding * 2, yy, ww, hh, "reset");
            auto remove = new Fl_Button(reset->x() + ww + padding * 2, yy, ww, hh, "remove");
            remove->color(FL_RED);
            group->end();
            widget = group;

            apply->callback([](auto, void* data){ static_cast<peer_device_t*>(data)->on_apply(); }, &container);
            reset->callback([](auto, void* data){ static_cast<peer_device_t*>(data)->on_reset(); }, &container);
            remove->callback([](auto, void* data){ static_cast<peer_device_t*>(data)->on_remove(); }, &container);

            return widget;
        }
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_name(peer_device_t &container) {
    struct widget_t final : peer_device_t::peer_widget_t {
        using parent_t = peer_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
            ww = std::min(300, ww);

            input = new Fl_Input(x + padding, yy, ww, hh);
            input->value(container.peer->get_name().data());

            group->end();
            group->resizable(nullptr);
            widget = group;
            return widget;
        }

        mutable Fl_Input *input;
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_introducer(peer_device_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto widget = parent_t::create_widget(x, y, w, h);
            input->value(container.peer->is_introducer());
            return widget;
        }
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_auto_accept(peer_device_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto widget = parent_t::create_widget(x, y, w, h);
            input->value(container.peer->has_auto_accept());
            return widget;
        }
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_paused(peer_device_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto widget = parent_t::create_widget(x, y, w, h);
            input->value(container.peer->is_paused());
            return widget;
        }
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_compressions(peer_device_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
            ww = std::min(150, ww);

            input = new Fl_Choice(x + padding, yy, ww, hh);
            input->add("metadata");
            input->add("never");
            input->add("always");
            input->value(0);

            group->end();
            group->resizable(nullptr);
            widget = group;
            return widget;
        }
        mutable Fl_Choice *input;
    };

    return new widget_t(container);
}

peer_device_t::peer_device_t(model::device_ptr_t peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), peer{std::move(peer_)} {
    auto name = peer->get_name();
    auto label = fmt::format("{}, {}", name, peer->device_id().get_short());
    this->label(label.data());
}

void peer_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        auto group = new Fl_Tile(x, y, w, h);
        auto resizeable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizeable_area);

        group->begin();

        auto top = [&]() -> Fl_Widget * {
            auto data = table_rows_t();

            auto device_id = peer->device_id().get_value();
            auto device_id_short = peer->device_id().get_short();
            auto cert_name = peer->get_cert_name();

            data.push_back({"name", record(make_name(*this))});
            data.push_back({"state", get_state()});
            data.push_back({"cert name", cert_name.value_or("")});
            data.push_back({"device id (short)", std::string(device_id_short)});
            data.push_back({"device id", device_id});
            data.push_back({"introducer", record(make_introducer(*this))});
            data.push_back({"auto accept", record(make_auto_accept(*this))});
            data.push_back({"paused", record(make_paused(*this))});
            data.push_back({"compression", record(make_compressions(*this))});

            data.push_back({"actions", record(make_actions(*this))});
            table = new static_table_t(std::move(data), x, y, w, h - bot_h);
            return table;
        }();
        auto bot = [&]() -> Fl_Widget * { return new qr_button_t(peer, supervisor, x, y + top->h(), w, bot_h); }();
        group->add(top);
        group->add(bot);
        group->end();
        return group;
    });
}

widgetable_ptr_t peer_device_t::record(peer_widget_ptr_t widget) {
    widgets.push_back(widget);
    return widget;
}

std::string peer_device_t::get_state() {
    switch (peer->get_state()) {
    case model::device_state_t::online:
        return "online";
    case model::device_state_t::dialing:
        return "dialing";
    default:
        return "offline";
    }
}


void peer_device_t::on_apply() {}
void peer_device_t::on_reset() {}
void peer_device_t::on_remove() {}
