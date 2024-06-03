#include "peer_device.h"

#include "../static_table.h"
#include "../qr_button.h"

#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static widgetable_ptr_t make_button(peer_device_t &) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        const Fl_Widget *get_widget() const override { return widget; }
        void create_widget(int x, int y, int w, int h) const { widget = new Fl_Button(x, y, w, h, "delete"); }

        mutable Fl_Widget *widget;
    };

    return new widget_t();
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

            data.push_back({"name", std::string(peer->get_name())});
            data.push_back({"device id (short)", std::string(device_id_short)});
            data.push_back({"device id", device_id});

            // auto button = new Fl_Button(0, 0, 50, 20, "delete");
            data.push_back({"actions", make_button(*this)});
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
