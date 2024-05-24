#include "devices.h"
#include "self_device.h"
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>

using namespace syncspirit::fltk::tree_item;

namespace {

struct devices_widget_t : Fl_Scroll {
    using parent_t = Fl_Scroll;

    devices_widget_t(int x, int y, int w, int h) : parent_t(x, y, w, h) {
        box(FL_FLAT_BOX);
        auto padding_top = 40;
        auto padding = 10;
        auto field_w = 570;
        auto field_h = 25;
        auto label_pad = 80;
        auto label = new Fl_Box(x + padding, y + padding, label_pad, field_h, "device id");
        auto input = new Fl_Input(x + padding + label_pad, y + padding, 570, field_h);
        auto pad_right = new Fl_Box(input->x() + input->w(), y + padding, padding, field_h);
        auto button_x = x + padding + label_pad;
        auto button = new Fl_Button(button_x, input->y() + field_h + padding, field_w, field_h, "add new device");
        // resizable(nullptr);
    }
};

} // namespace

devices_t::devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    label("devices");

    auto self_node = new tree_item::self_device_t(supervisor, tree);

    add(prefs(), "self", self_node);
}

void devices_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        return new devices_widget_t(prev->x(), prev->y(), prev->w(), prev->h());
    });
}
