#include "peer_device.h"

#include "../qr_button.h"

#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

Fl_Widget *peer_device_t::peer_widget_t::get_widget() { return widget; }

static peer_device_t::peer_widget_ptr_t make_actions(peer_device_t &container) {
    struct widget_t final : peer_device_t::peer_widget_t {
        using parent_t = peer_widget_t;

        widget_t(peer_device_t &container_) : container{container_} {}

        void create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w / 2 - padding / 2, hh = h - padding * 2;
            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            auto remove = new Fl_Button(apply->x() + apply->w() + padding / 2, yy, ww, hh, "remove");
            group->end();
            widget = group;
        }

        peer_device_t &container;
    };

    return new widget_t(container);
}

static peer_device_t::peer_widget_ptr_t make_name(peer_device_t &container) {
    struct widget_t final : peer_device_t::peer_widget_t {
        using parent_t = peer_widget_t;

        widget_t(peer_device_t &container_) : container{container_}, input{nullptr} {}

        void create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;

            input = new Fl_Input(x + padding, yy, ww, hh);
            input->value(container.peer->get_name().data());

            group->end();
            widget = group;
        }

        peer_device_t &container;
        mutable Fl_Input *input;
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

        // auto group = new Fl_Group(x, y, w, h);
        auto group = new Fl_Tile(x, y, w, h);
        auto resizeable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizeable_area);

        group->begin();

        auto top = [&]() -> Fl_Widget * {
            auto data = table_rows_t();

            auto device_id = peer->device_id().get_value();
            auto device_id_short = peer->device_id().get_short();

            data.push_back({"name", record(make_name(*this))});
            data.push_back({"device id (short)", std::string(device_id_short)});
            data.push_back({"device id", device_id});

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
