#include "self_device.h"

#include "qr_code.h"
#include "../static_table.h"
#include "constants.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>

using namespace syncspirit::fltk::tree_item;

static void on_timeout(void *data) {
    auto self = reinterpret_cast<self_device_t *>(data);
    auto table = static_cast<syncspirit::fltk::static_table_t *>(self->table);
    table->update_value(2, self->supervisor.get_uptime());
    Fl::repeat_timeout(1.0, on_timeout, data);
}

self_device_t::self_device_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    supervisor.add(this);
    label("self");

    auto qt_code = new qr_code_t(supervisor, tree);
    add(prefs(), "qr_code", qt_code);
}

self_device_t::~self_device_t() {
    Fl::remove_timeout(on_timeout, this);
    supervisor.remove(this);
}

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
        table = new static_table_t(std::move(data), prev->x(), prev->y(), prev->w(), prev->h());
        return table;
    });
    Fl::add_timeout(1.0, on_timeout, this);
}
