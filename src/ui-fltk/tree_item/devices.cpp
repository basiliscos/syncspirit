#include "devices.h"
#include "self_device.h"
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>

#include "model/device_id.h"
#include "model/diff/modify/update_peer.h"
#include "utils/format.hpp"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

struct devices_widget_t : Fl_Scroll {
    using parent_t = Fl_Scroll;

    devices_widget_t(app_supervisor_t &supervisor_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), supervisor{supervisor_} {
        box(FL_FLAT_BOX);
        auto padding_top = 40;
        auto padding = 10;
        auto field_w = 570;
        auto field_h = 25;
        auto label_pad = 80;
        auto field_x = x + padding * 2 + label_pad;
        auto label_id = new Fl_Box(x + padding, y + padding, label_pad, field_h, "device id");
        label_id->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        input_id = new Fl_Input(field_x, label_id->y(), 570, field_h);

        auto label_label = new Fl_Box(label_id->x(), input_id->y() + field_h + padding, label_pad, field_h, "label");
        label_label->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        input_label = new Fl_Input(field_x, label_label->y(), 570, field_h);

        auto pad_right = new Fl_Box(input_id->x() + input_id->w(), y + padding, padding, field_h);
        auto button =
            new Fl_Button(input_label->x(), input_label->y() + field_h + padding, field_w, field_h, "add new device");
        button->callback([](auto, void *data) { reinterpret_cast<devices_widget_t *>(data)->on_add_device(); }, this);
        // resizable(nullptr);
    }

    void on_add_device() {
        auto device_opt = model::device_id_t::from_string(input_id->value());
        auto &log = supervisor.get_logger();
        if (!device_opt) {
            log->error("incorrect device_id");
            return;
        }

        auto &peer = *device_opt;
        auto &devices = supervisor.get_cluster()->get_devices();
        auto found = devices.by_sha256(peer.get_sha256());
        if (found) {
            log->error("device {} is already added", peer);
            return;
        }

        db::Device db_dev;
        db_dev.set_name(input_label->value());

        auto diff = cluster_diff_ptr_t(new modify::update_peer_t(std::move(db_dev), peer.get_sha256()));
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    app_supervisor_t &supervisor;
    Fl_Input *input_id;
    Fl_Input *input_label;
};

} // namespace

devices_t::devices_t(app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), model_sub(supervisor.add(this)) {
    label("devices");
    build_tree();
}

void devices_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        return new devices_widget_t(supervisor, prev->x(), prev->y(), prev->w(), prev->h());
    });
}

void devices_t::operator()(model::message::model_response_t &) {
    clear_children();
    build_tree();
}

void devices_t::build_tree() {
    if (children()) {
        return;
    }
    auto &cluster = supervisor.get_cluster();
    if (!cluster) {
        return;
    }
    auto &devices = cluster->get_devices();

    tree()->begin();
    auto self_node = new tree_item::self_device_t(supervisor, tree());
    add(prefs(), "self", self_node);

    for (auto it : devices) {
        if (it.item == cluster->get_device()) {
            continue;
        }
        add_device(it.item);
    }
    tree()->end();
}

void devices_t::add_device(const model::device_ptr_t &device) {
    auto device_node = new tree_item_t(supervisor, tree());
    auto label = fmt::format("{}, {}", device->get_name(), device->device_id().get_short());
    device_node->label(label.data());
}
