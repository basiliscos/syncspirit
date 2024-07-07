#include "devices.h"
#include "self_device.h"
#include "peer_device.h"
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>

#include "model/device_id.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/load/load_cluster.h"
#include "utils/format.hpp"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

struct devices_widget_t : Fl_Scroll {
    using parent_t = Fl_Scroll;

    devices_widget_t(devices_t &owner_, int x, int y, int w, int h) : parent_t(x, y, w, h), owner{owner_} {
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

    void on_add_device() { owner.add_new_device(input_id->value(), input_label->value()); }

    devices_t &owner;
    Fl_Input *input_id;
    Fl_Input *input_label;
};

} // namespace

devices_t::devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree), devices_count{0} {
    supervisor.set_devices(this);
    update_label();
}

bool devices_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto widget = new devices_widget_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
        content = widget;
        return widget;
    });
    return true;
}

void devices_t::update_label() {
    auto l = fmt::format("devices ({})", devices_count);
    this->label(l.data());
}

#if 0
void devices_t::operator()(model::message::model_update_t &update) {
    std::ignore = update.payload.diff->visit(*this, nullptr);
    build_tree();
    update_label();
}

auto devices_t::operator()(const diff::modify::update_peer_t &diff, void *) noexcept -> outcome::result<void> {
    auto peer = supervisor.get_cluster()->get_devices().by_sha256(diff.peer_id);
    bool new_peer = true;
    for (int i = 1; i < children(); ++i) {
        auto node = dynamic_cast<peer_device_t *>(child(i));
        if (node) {
            if (node->peer == peer) {
                new_peer = true;
                break;
            }
        }
    }
    if (new_peer) {
        auto tree_item = add_device(peer);
        if (tree_item) {
            tree()->select(tree_item, 1);
            tree()->deselect(this, 1);
        }
    }
    return outcome::success();
}

auto devices_t::operator()(const diff::load::load_cluster_t &diff, void *data) noexcept -> outcome::result<void> {
    return diff.diff::cluster_aggregate_diff_t::visit(*this, data);
}

auto devices_t::operator()(const diff::load::devices_t &diff, void *) noexcept -> outcome::result<void> {
    build_tree();
    update_label();
    return outcome::success();
}

void devices_t::build_tree() {
    auto &cluster = supervisor.get_cluster();
    if (!cluster) {
        return;
    }
    auto &devices = cluster->get_devices();

    tree()->begin();
    if (children() == 0) {
        auto self_node = new tree_item::self_device_t(supervisor, tree());
        add(prefs(), "self", self_node);
        tree()->close(self_node, 0);
    }

    for (auto it : devices) {
        if (it.item == cluster->get_device()) {
            continue;
        }
        add_device(it.item);
    }
    devices_count = devices.size();
    tree()->end();
}

tree_item_t *devices_t::get_self_device() { return static_cast<tree_item_t *>(child(0)); }

auto devices_t::add_device(const model::device_ptr_t &device) -> tree_item_t * {
    for (int i = 1; i < children(); ++i) {
        auto node = dynamic_cast<peer_device_t *>(child(i));
        if (node) {
            if (node->peer == device) {
                return nullptr;
            }
        }
    }

    auto device_node = new peer_device_t(device, supervisor, tree());
    add(prefs(), device_node->label(), device_node);
    tree()->close(device_node, 0);
    return device_node;
}

#endif
void devices_t::add_new_device(std::string_view device_id, std::string_view label) {
    auto device_opt = model::device_id_t::from_string(device_id);
    auto &log = supervisor.get_logger();
    if (!device_opt) {
        log->error("incorrect device_id");
        return;
    }

    auto &peer = *device_opt;
    auto &cluster = *supervisor.get_cluster();
    auto &devices = cluster.get_devices();
    auto found = devices.by_sha256(peer.get_sha256());
    if (found) {
        log->error("device {} is already added", peer);
        return;
    }

    db::Device db_dev;
    db_dev.set_name(std::string(label));

    auto diff = cluster_diff_ptr_t(new modify::update_peer_t(std::move(db_dev), peer, cluster));
    supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
}

auto devices_t::set_self(model::device_t &self) -> augmentation_ptr_t {
    return within_tree([&]() {
        auto self_node = new tree_item::self_device_t(self, supervisor, tree());
        add(prefs(), "self", self_node);
        tree()->close(self_node, 0);
        return self_node->get_proxy();
    });
}

augmentation_ptr_t devices_t::add_peer(model::device_t &peer) {
    ++devices_count;
    update_label();
    return within_tree([&]() {
        auto device_node = new peer_device_t(peer, supervisor, tree());
        add(prefs(), device_node->label(), device_node);
        return device_node->get_proxy();
    });
}

void devices_t::remove_peer(tree_item_t *item) {
    --devices_count;
    update_label();
    remove_child(item);
    tree()->redraw();
}
