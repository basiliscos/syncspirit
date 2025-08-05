// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "pending_device.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/remove_pending_device.h"
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

namespace {

struct my_table_t;

static widgetable_ptr_t make_actions(my_table_t &container);

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(pending_device_t &container_, int x, int y, int w, int h) : parent_t(x, y, w, h), container{container_} {
        auto &device = container.device;
        auto &device_id = device.get_device_id();
        auto data = table_rows_t();
        auto last_seen = model::pt::to_simple_string(device.get_last_seen());

        name_cell = new static_string_provider_t();
        client_name_cell = new static_string_provider_t();
        client_version_cell = new static_string_provider_t();
        address_cell = new static_string_provider_t();
        last_seen_cell = new static_string_provider_t();

        data.push_back({"name", name_cell});
        data.push_back({"device id (short)", new static_string_provider_t(device_id.get_short())});
        data.push_back({"device id", new static_string_provider_t(device_id.get_value())});
        data.push_back({"client", client_name_cell});
        data.push_back({"client version", client_version_cell});
        data.push_back({"address", address_cell});
        data.push_back({"last_seen", last_seen_cell});
        data.push_back({"actions", make_actions(*this)});
        assign_rows(std::move(data));
        refresh();
    }

    void refresh() override {
        auto &device = container.device;
        name_cell->update(device.get_name());
        client_name_cell->update(device.get_client_name());
        client_version_cell->update(device.get_client_version());
        address_cell->update(device.get_address());
        name_cell->update(device.get_name());
        last_seen_cell->update(model::pt::to_simple_string(device.get_last_seen()));
        redraw();
    }

    void on_connect() {
        auto &device = container.device;
        auto &supervisor = container.supervisor;
        db::Device db_dev;
        db::set_name(db_dev, device.get_name());

        auto diff = cluster_diff_ptr_t();
        auto &cluster = *supervisor.get_cluster();
        diff = new modify::update_peer_t(std::move(db_dev), device.get_device_id(), cluster);
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    void on_ignore() {
        auto &device = container.device;
        auto &supervisor = container.supervisor;
        auto diff = cluster_diff_ptr_t{};
        auto &cluster = *supervisor.get_cluster();
        db::SomeDevice db;
        device.serialize(db);
        diff = new modify::add_ignored_device_t(cluster, device.get_device_id(), std::move(db));
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    void on_remove() {
        auto &device = container.device;
        auto &supervisor = container.supervisor;
        auto diff = cluster_diff_ptr_t{};
        diff = new modify::remove_pending_device_t(device);
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    pending_device_t &container;
    static_string_provider_ptr_t name_cell;
    static_string_provider_ptr_t client_name_cell;
    static_string_provider_ptr_t client_version_cell;
    static_string_provider_ptr_t address_cell;
    static_string_provider_ptr_t last_seen_cell;
};

static widgetable_ptr_t make_actions(my_table_t &container) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        widget_t(my_table_t &container_) : parent_t(container_) {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto connect = new Fl_Button(x + padding, yy, ww, hh, "connect");
            auto ignore = new Fl_Button(connect->x() + ww + padding * 2, yy, ww, hh, "ignore");
            auto remove = new Fl_Button(ignore->x() + ww + padding * 2, yy, ww, hh, "remove");
            remove->color(FL_RED);

            group->resizable(nullptr);
            group->end();

            connect->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_connect(); }, &container);
            ignore->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_ignore(); }, &container);
            remove->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_remove(); }, &container);

            widget = group;
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

pending_device_t::pending_device_t(model::pending_device_t &device_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), device{device_} {
    update_label();
}

void pending_device_t::update_label() {
    auto name = device.get_name();
    auto id = device.get_device_id().get_short();
    auto value = fmt::format("{}, {}", name, id);
    label(value.data());
    tree()->redraw();
}

bool pending_device_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        struct tile_t : contentable_t<Fl_Tile> {
            using parent_t = contentable_t<Fl_Tile>;
            using parent_t::parent_t;
        };

        auto prev = content->get_widget();
        auto &device_id = device.get_device_id();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        auto group = new tile_t(x, y, w, h);
        auto resizable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizable_area);

        group->begin();
        auto top = [&]() -> Fl_Widget * {
            int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
            return new my_table_t(*this, x, y, w, h - bot_h);
        }();
        auto bot = [&]() -> Fl_Widget * { return new qr_button_t(device_id, supervisor, x, y + top->h(), w, bot_h); }();
        group->add(top);
        group->add(bot);
        group->end();
        return group;
    });
    return true;
}
