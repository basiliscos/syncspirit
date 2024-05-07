#include "self_device.h"

using namespace syncspirit::fltk::tree_item;

self_device_t::self_device_t(app_supervisor_t &supervisor, Fl_Tree* tree):parent_t(supervisor, tree) {
    supervisor.add(this);
    label("self");
}

self_device_t::~self_device_t() {
    supervisor.remove(this);
}

void self_device_t::operator()(model::message::model_response_t&) {
    auto& self = *supervisor.get_cluster()->get_device();
    auto device_id = std::string(self.device_id().get_short());
    label(device_id.data());
}
