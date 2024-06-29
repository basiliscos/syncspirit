// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#include "unknown_device.h"
#include "../static_table.h"
#include "../qr_button.h"

#include <FL/Fl_Tile.H>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace fltk;
using namespace fltk::tree_item;

unknown_device_t::unknown_device_t(model::unknown_device_ptr_t device_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), device{std::move(device_)} {
    update_label();
}

void unknown_device_t::update_label() {
    auto name = device->get_name();
    auto device_id = model::device_id_t::from_sha256(device->get_sha256()).value();
    auto id = device_id.get_short();
    auto value = fmt::format("{}, {}", name, id);
    label(value.data());
    tree()->redraw();
}

void unknown_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto device_id = model::device_id_t::from_sha256(device->get_sha256()).value();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        auto group = new Fl_Tile(x, y, w, h);
        auto resizeable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizeable_area);

        group->begin();
        auto top = [&]() -> Fl_Widget * {
            auto data = table_rows_t();
            auto last_seen = model::pt::to_simple_string(device->get_last_seen());
            data.push_back({"name", std::string(device->get_name())});
            data.push_back({"device id (short)", std::string(device_id.get_short())});
            data.push_back({"device id", std::string(device_id.get_value())});
            data.push_back({"client", std::string(device->get_client_name())});
            data.push_back({"client version", std::string(device->get_client_version())});
            data.push_back({"address", std::string(device->get_address())});
            data.push_back({"last_seen", last_seen});
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
}

void unknown_device_t::refresh() {
    update_label();
    if (content) {
        auto &table = *static_cast<static_table_t *>(content);
        auto &rows = table.get_rows();
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].label == "name") {
                table.update_value(i, std::string(device->get_name()));
            } else if (rows[i].label == "client") {
                table.update_value(i, std::string(device->get_client_name()));
            } else if (rows[i].label == "client version") {
                table.update_value(i, std::string(device->get_client_version()));
            } else if (rows[i].label == "address") {
                table.update_value(i, std::string(device->get_address()));
            } else if (rows[i].label == "last_seen") {
                auto last_seen = model::pt::to_simple_string(device->get_last_seen());
                table.update_value(i, last_seen);
            }
        }
        content->redraw();
    }
}
