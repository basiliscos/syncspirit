#include "self_device.h"

#include "../static_table.h"
#include "constants.h"
#include <spdlog/fmt/fmt.h>
#include <FL/Fl_Box.H>

using namespace syncspirit::fltk::tree_item;

self_device_t::self_device_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    supervisor.add(this);
    label("self");
}

self_device_t::~self_device_t() { supervisor.remove(this); }

void self_device_t::operator()(model::message::model_response_t &) {
    auto &self = *supervisor.get_cluster()->get_device();
    auto device_id = self.device_id().get_short();
    auto label = fmt::format("self: {}", device_id);
    this->label(label.data());
    recalc_tree();
    tree()->redraw();
}

void self_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto &self = *supervisor.get_cluster()->get_device();
        auto device_id = self.device_id();
        auto data = table_rows_t();
        data.push_back({"device id (short)", std::string(device_id.get_short())});
        data.push_back({"device id", std::string(device_id.get_value())});
        data.push_back({"uptime", supervisor.get_uptime()});
        data.push_back({"version", fmt::format("{} {}", constants::client_name, constants::client_version)});
        auto widget = new static_table_t(std::move(data), prev->x(), prev->y(), prev->w(), prev->h());
        return widget;
    });
}
