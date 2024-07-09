#include "self_device.h"

#include "settings.h"
#include "../static_table.h"
#include "../qr_button.h"
#include "constants.h"

#include <spdlog/fmt/fmt.h>
#include <FL/Fl.H>
#include <FL/Fl_Tile.H>

#include <lz4.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include "mdbx.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

static void on_uptime_timeout(void *data);
static void on_db_refresh_timeout(void *data);

struct self_table_t final : static_table_t, db_info_viewer_t {
    using parent_t = static_table_t;
    using db_info_t = syncspirit::net::payload::db_info_response_t;

    self_table_t(self_device_t *owner_, table_rows_t &&rows, int x, int y, int w, int h)
        : parent_t(std::move(rows), x, y, w, h), owner{owner_}, db_info_guard(nullptr) {
        update_db_info();
        Fl::add_timeout(1.0, on_uptime_timeout, owner);
        Fl::add_timeout(10.0, on_db_refresh_timeout, owner);
    }

    ~self_table_t() {
        Fl::remove_timeout(on_uptime_timeout, owner);
        Fl::remove_timeout(on_db_refresh_timeout, owner);
    }

    void update_db_info() { db_info_guard = owner->supervisor.request_db_info(this); }

    void view(const db_info_t &info) override {
        auto &rows = get_rows();
        auto pages = info.leaf_pages + info.ms_branch_pages + info.overflow_pages;
        auto size = pages * info.page_size / 1024;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].label == "mdbx entries") {
                update_value(i, std::to_string(info.entries));
            } else if (rows[i].label == "mdbx pages") {
                update_value(i, std::to_string(pages));
            } else if (rows[i].label == "mdbx size, Kb") {
                update_value(i, std::to_string(size));
            }
        }
        redraw();
    }

    self_device_t *owner;
    db_info_viewer_guard_t db_info_guard;
};

static void on_uptime_timeout(void *data) {
    auto self = reinterpret_cast<self_device_t *>(data);
    auto table = static_cast<self_table_t *>(self->content);
    if (table) {
        table->update_value(2, self->supervisor.get_uptime());
        table->redraw();
        Fl::repeat_timeout(1.0, on_uptime_timeout, data);
    }
}
static void on_db_refresh_timeout(void *data) {
    auto self = reinterpret_cast<self_device_t *>(data);
    auto table = static_cast<self_table_t *>(self->content);
    if (table) {
        table->update_db_info();
        Fl::repeat_timeout(10.0, on_db_refresh_timeout, data);
    }
}

} // namespace

self_device_t::self_device_t(model::device_t &self_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree) {

    auto settings = new settings_t(supervisor, tree);
    add(prefs(), "settings", settings);

    update_label();
}

void self_device_t::update_label() {
    auto self = supervisor.get_cluster()->get_device();
    auto device_id = self->device_id().get_short();
    auto label = fmt::format("(self) {}, {}", supervisor.get_app_config().device_name, device_id);
    this->label(label.data());
}

bool self_device_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        // auto group = new Fl_Group(x, y, w, h);
        auto group = new Fl_Tile(x, y, w, h);
        auto resizeable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizeable_area);

        group->begin();
        auto top = [&]() -> Fl_Widget * {
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

            auto openssl_version =
                fmt::format("{}.{}.{}{}", openssl_major, openssl_minor, openssl_patch, openssl_nibble_c);
            auto mbdx_version = fmt::format("{}.{}.{}", mdbx_version.major, mdbx_version.minor, mdbx_version.release);

            auto data = table_rows_t();
            data.push_back({"device id (short)", device_id_short});
            data.push_back({"device id", device_id});
            data.push_back({"uptime", supervisor.get_uptime()});
            data.push_back({"mdbx entries", ""});
            data.push_back({"mdbx pages", ""});
            data.push_back({"mdbx size, Kb", ""});
            data.push_back({"app version", fmt::format("{} {}", constants::client_name, constants::client_version)});
            data.push_back({"mdbx version", mbdx_version});
            data.push_back({"protobuf version", google::protobuf::internal::VersionString(GOOGLE_PROTOBUF_VERSION)});
            data.push_back({"lz4 version", LZ4_versionString()});
            data.push_back({"openssl version", openssl_version});
            data.push_back({"fltk version", fmt::format("{}", Fl::version())});

            content = new self_table_t(this, std::move(data), x, y, w, h - bot_h);
            return content;
        }();
        auto bot = [&]() -> Fl_Widget * {
            auto &device = supervisor.get_cluster()->get_device()->device_id();
            return new qr_button_t(device, supervisor, x, y + top->h(), w, bot_h);
        }();
        group->add(top);
        group->add(bot);
        group->end();
        return group;
    });
    return true;
}
