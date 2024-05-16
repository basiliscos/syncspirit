#include "self_device.h"

#include "qr_code.h"
#include "../static_table.h"
#include "constants.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <lz4.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include "mdbx.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static void on_timeout(void *data) {
    auto self = reinterpret_cast<self_device_t *>(data);
    auto table = static_cast<syncspirit::fltk::static_table_t *>(self->table);
    table->update_value(2, self->supervisor.get_uptime());
    Fl::repeat_timeout(1.0, on_timeout, data);
}

namespace {

struct my_table_t : static_table_t {
    using parent_t = static_table_t;
    my_table_t(void *timer_canceller_, table_rows_t &&rows, int x, int y, int w, int h)
        : parent_t(std::move(rows), x, y, w, h), timer_canceller{timer_canceller_} {}

    ~my_table_t() { Fl::remove_timeout(on_timeout, timer_canceller); }

    void *timer_canceller;
};

} // namespace

self_device_t::self_device_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree), table{nullptr} {
    supervisor.add(this);
    label("self");

    auto qt_code = new qr_code_t(supervisor, tree);
    add(prefs(), "qr_code", qt_code);
}

self_device_t::~self_device_t() {
    supervisor.get_logger()->trace("~self_device_t");
    supervisor.remove(this);
}

void self_device_t::operator()(model::message::model_response_t &) {
    auto &self = *supervisor.get_cluster()->get_device();
    auto device_id = self.device_id().get_short();
    auto label = fmt::format("self: {}", device_id);
    this->label(label.data());
    recalc_tree();
    tree()->redraw();

    if (table) {
        auto my_table = static_cast<my_table_t *>(table);
        auto &id = supervisor.get_cluster()->get_device()->device_id();
        auto device_id_short = id.get_short();
        auto device_id = id.get_value();
        my_table->update_value(0, std::string(device_id_short));
        my_table->update_value(1, std::string(device_id));
    }
}

void self_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto data = table_rows_t();

        auto cluster = supervisor.get_cluster();
        auto device_id_short = std::string("XXXXXXX");
        auto device_id = std::string("XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX");
        if (cluster) {
            auto &id = supervisor.get_cluster()->get_device()->device_id();
            device_id_short = id.get_short();
            device_id = id.get_value();
        }

        auto v = OPENSSL_VERSION_NUMBER;
        // clang-format off
        //                     0x1010113fL
        auto openssl_major  = (0xF0000000L & OPENSSL_VERSION_NUMBER) >> 7 * 4;
        auto openssl_minor  = (0x0FF00000L & OPENSSL_VERSION_NUMBER) >> 5 * 4;
        auto openssl_patch  = (0x000FF000L & OPENSSL_VERSION_NUMBER) >> 3 * 4;
        auto openssl_nibble = (0x00000FF0L & OPENSSL_VERSION_NUMBER) >> 1 * 4;
        // clang-format on
        auto openssl_nibble_c = static_cast<char>(openssl_nibble);
        if (openssl_nibble) {
            openssl_nibble_c += 'a' - 1;
        }

        auto openssl_version = fmt::format("{}.{}.{}{}", openssl_major, openssl_minor, openssl_patch, openssl_nibble_c);
        auto mbdx_version = fmt::format("{}.{}.{}", mdbx_version.major, mdbx_version.minor, mdbx_version.release);

        data.push_back({"device id (short)", device_id_short});
        data.push_back({"device id", device_id});
        data.push_back({"uptime", supervisor.get_uptime()});
        data.push_back({"app version", fmt::format("{} {}", constants::client_name, constants::client_version)});
        data.push_back({"mdbx version", mbdx_version});
        data.push_back({"protobuf version", google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION)});
        data.push_back({"lz4 version", LZ4_versionString()});
        data.push_back({"openssl version", openssl_version});
        data.push_back({"fltk version", fmt::format("{}", Fl::version())});

        table = new my_table_t(this, std::move(data), prev->x(), prev->y(), prev->w(), prev->h());
        return table;
    });
    Fl::add_timeout(1.0, on_timeout, this);
}
