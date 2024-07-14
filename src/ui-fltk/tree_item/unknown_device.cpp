// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_device.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/remove_unknown_device.h"
#include "model/diff/modify/update_peer.h"
#include "../static_table.h"
#include "../qr_button.h"

#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

static constexpr int padding = 2;

static widgetable_ptr_t make_actions(unknown_device_t &container) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        widget_t(unknown_device_t &container_) : parent_t(container_) {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto connect = new Fl_Button(x + padding, yy, ww, hh, "connect");
            auto ignore = new Fl_Button(connect->x() + ww + padding * 2, yy, ww, hh, "ignore");
            auto remove = new Fl_Button(ignore->x() + ww + padding * 2, yy, ww, hh, "remove");
            remove->color(FL_RED);
            group->end();

            connect->callback([](auto, void *data) { static_cast<unknown_device_t *>(data)->on_connect(); },
                              &container);
            ignore->callback([](auto, void *data) { static_cast<unknown_device_t *>(data)->on_ignore(); }, &container);
            remove->callback([](auto, void *data) { static_cast<unknown_device_t *>(data)->on_remove(); }, &container);

            widget = group;
            return widget;
        }
    };

    return new widget_t(container);
}

unknown_device_t::unknown_device_t(model::unknown_device_t &device_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), device{device_} {
    update_label();
}

void unknown_device_t::update_label() {
    auto name = device.get_name();
    auto id = device.get_device_id().get_short();
    auto value = fmt::format("{}, {}", name, id);
    label(value.data());
    tree()->redraw();
}

bool unknown_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto &device_id = device.get_device_id();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        auto group = new Fl_Tile(x, y, w, h);
        auto resizeable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizeable_area);

        group->begin();
        auto top = [&]() -> Fl_Widget * {
            auto data = table_rows_t();
            auto last_seen = model::pt::to_simple_string(device.get_last_seen());
            data.push_back({"name", std::string(device.get_name())});
            data.push_back({"device id (short)", std::string(device_id.get_short())});
            data.push_back({"device id", std::string(device_id.get_value())});
            data.push_back({"client", std::string(device.get_client_name())});
            data.push_back({"client version", std::string(device.get_client_version())});
            data.push_back({"address", std::string(device.get_address())});
            data.push_back({"last_seen", last_seen});
            data.push_back({"actions", make_actions(*this)});
            int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
            content = new static_table_t(std::move(data), x, y, w, h - bot_h);
            return content;
        }();
        auto bot = [&]() -> Fl_Widget * { return new qr_button_t(device_id, supervisor, x, y + top->h(), w, bot_h); }();
        group->add(top);
        group->add(bot);
        group->end();
        return group;
    });
    return true;
}

void unknown_device_t::refresh_content() {
    update_label();
    if (content) {
        auto &table = *static_cast<static_table_t *>(content);
        auto &rows = table.get_rows();
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].label == "name") {
                table.update_value(i, std::string(device.get_name()));
            } else if (rows[i].label == "client") {
                table.update_value(i, std::string(device.get_client_name()));
            } else if (rows[i].label == "client version") {
                table.update_value(i, std::string(device.get_client_version()));
            } else if (rows[i].label == "address") {
                table.update_value(i, std::string(device.get_address()));
            } else if (rows[i].label == "last_seen") {
                auto last_seen = model::pt::to_simple_string(device.get_last_seen());
                table.update_value(i, last_seen);
            }
        }
        content->redraw();
    }
}

void unknown_device_t::on_connect() {
    db::Device db_dev;
    db_dev.set_name(std::string(device.get_name()));

    auto diff = cluster_diff_ptr_t();
    auto &cluster = *supervisor.get_cluster();
    diff = new modify::update_peer_t(std::move(db_dev), device.get_device_id(), cluster);
    supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
}

void unknown_device_t::on_ignore() {
    auto diff = cluster_diff_ptr_t{};
    auto &cluster = *supervisor.get_cluster();
    db::SomeDevice db;
    device.serialize(db);
    diff = new modify::add_ignored_device_t(cluster, device.get_device_id(), std::move(db));
    supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
}

void unknown_device_t::on_remove() {
    auto diff = cluster_diff_ptr_t{};
    diff = new modify::remove_unknown_device_t(device);
    supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
}
