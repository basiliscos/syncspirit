// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "devices.h"
#include "self_device.h"
#include "../table_widget/input.h"
#include "../table_widget/label.h"
#include "../static_table.h"
#include "model/device_id.h"
#include "model/diff/modify/update_peer.h"
#include "utils/format.hpp"

#include <FL/Fl_Scroll.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Box.H>

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

namespace {

struct my_table_t;

struct serialization_context_t {
    std::string_view device_id;
    std::string_view label;
};

auto static make_device_id(my_table_t &container) -> widgetable_ptr_t;
auto static make_label(my_table_t &container) -> widgetable_ptr_t;
auto static make_notice(my_table_t &container) -> widgetable_ptr_t;
auto static make_actions(my_table_t &container) -> widgetable_ptr_t;

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(devices_t &container_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), container{container_}, add_button{nullptr} {

        auto data = table_rows_t();

        data.push_back({"device_id", make_device_id(*this)});
        data.push_back({"label", make_label(*this)});
        data.push_back({"", notice = make_notice(*this)});
        data.push_back({"actions", make_actions(*this)});
        assign_rows(std::move(data));
    }

    void refresh() override {
        serialization_context_t ctx{};
        store(&ctx);

        auto device_opt = std::optional<model::device_id_t>{};

        error = "";
        if (!ctx.device_id.empty()) {
            device_opt = model::device_id_t::from_string(ctx.device_id);
            if (device_opt) {
                if (ctx.label.empty()) {
                    error = "no label given";
                }
            } else {
                error = "invalid device id";
            }
        }

        if (!ctx.device_id.empty() && error.empty()) {
            auto &peer = *device_opt;
            auto cluster = container.supervisor.get_cluster();
            auto &devices = cluster->get_devices();
            auto found = devices.by_sha256(peer.get_sha256());
            if (found) {
                error = "device already added";
            }
        }

        if (!error.empty() || ctx.device_id.empty()) {
            add_button->deactivate();
        } else {
            add_button->activate();
        }
        notice->reset();
    }

    void add_new_device() {
        serialization_context_t ctx{};
        store(&ctx);

        auto device_opt = model::device_id_t::from_string(ctx.device_id);
        auto &supervisor = container.supervisor;
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
        db_dev.name(ctx.label);

        auto diff = cluster_diff_ptr_t(new modify::update_peer_t(std::move(db_dev), peer, cluster));
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    widgetable_ptr_t notice;
    std::string_view error;
    devices_t &container;
    Fl_Widget *add_button;
};

auto static make_device_id(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialization_context_t *>(data);
            ctx->device_id = input->value();
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_label(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<serialization_context_t *>(data);
            ctx->label = input->value();
            return true;
        }
    };
    return new widget_t(container);
}

auto static make_notice(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;
        using parent_t::parent_t;

        void reset() override {
            auto label = static_cast<my_table_t &>(container).error;
            auto ptr = label.size() ? label.data() : "";
            input->label(ptr);
        }
    };
    return new widget_t(container);
}

auto static make_actions(my_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 160, hh = h - padding * 2;
            auto add = new Fl_Button(x + padding, yy, ww, hh, "add new device");
            add->callback([](auto, void *data) { static_cast<my_table_t *>(data)->add_new_device(); }, &container);
            add->deactivate();

            group->resizable(nullptr);

            group->end();
            widget = group;

            this->reset();
            auto &container = static_cast<my_table_t &>(this->container);
            container.add_button = add;
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

devices_t::devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    supervisor.set_devices(this);
    update_label();
}

bool devices_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        return new my_table_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
    });
    return true;
}

void devices_t::update_label() {
    auto devices_count = std::size_t{0};
    auto cluster = supervisor.get_cluster();
    if (cluster) {
        devices_count = cluster->get_devices().size();
        if (devices_count > 1) {
            --devices_count; // dont count self
        }
    }
    auto l = fmt::format("devices ({})", devices_count);
    this->label(l.data());
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
    update_label();
    return within_tree([&]() { return insert_by_label(new peer_device_t(peer, supervisor, tree()), 1)->get_proxy(); });
}

peer_device_t *devices_t::get_peer(const model::device_t &peer) {
    for (int i = 1; i < children(); ++i) {
        auto node = static_cast<peer_device_t *>(child(i));
        if (node->peer == peer) {
            return node;
        }
    }
    supervisor.get_logger()->warn("no node/tree-item for device {}", peer.device_id().get_short());
    return {};
}

void devices_t::remove_child(tree_item_t *child) {
    parent_t::remove_child(child);
    update_label();
}
