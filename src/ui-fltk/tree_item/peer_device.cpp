#include "peer_device.h"

#include "qr_code.h"
#include "../static_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

peer_device_t::peer_device_t(model::device_ptr_t peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), peer{std::move(peer_)} {
    auto name = peer->get_name();
    auto label = fmt::format("{}, {}", name, peer->device_id().get_short());
    this->label(label.data());

    auto qr_code = new qr_code_t(supervisor, tree);
    qr_code->set_device(peer);
    add(prefs(), "qr_code", qr_code);
}

void peer_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto data = table_rows_t();

        auto cluster = supervisor.get_cluster();
        auto device_id = peer->device_id().get_value();
        auto device_id_short = peer->device_id().get_short();

        data.push_back({"name", peer->get_name()});
        data.push_back({"device id (short)", device_id_short});
        data.push_back({"device id", device_id});
        table = new static_table_t(std::move(data), prev->x(), prev->y(), prev->w(), prev->h());
        return table;
    });
}
