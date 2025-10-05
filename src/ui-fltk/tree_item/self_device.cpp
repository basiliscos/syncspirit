// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "self_device.h"

#include "settings.h"
#include "../static_table.h"
#include "../qr_button.h"
#include "../utils.hpp"
#include "../main_window.h"
#include "utils/dns.h"
#include "constants.h"

#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl.H>
#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>

#include <lz4.h>
#include <spdlog/fmt/fmt.h>
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#include <mdbx.h>
#include <boost/version.hpp>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

static constexpr int padding = 2;

struct self_table_t;

static auto make_actions(self_table_t &container) -> widgetable_ptr_t;
static void on_uptime_timeout(void *data);
static void on_db_refresh_timeout(void *data);

struct self_table_t final : static_table_t, db_info_viewer_t {
    using parent_t = static_table_t;
    using db_info_t = syncspirit::net::payload::db_info_response_t;

    self_table_t(main_window_t *owner_, self_device_t *tree_node_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), owner{owner_}, tree_node{tree_node_}, db_info_guard(nullptr) {

        auto v = OPENSSL_VERSION_NUMBER;
        // clang-format off
        //                     0x1010113fL
        auto openssl_major  = (0xF0000000L & v) >> 7 * 4;
        auto openssl_minor  = (0x0FF00000L & v) >> 5 * 4;
        auto openssl_patch  = (0x000FF000L & v) >> 3 * 4;
        auto openssl_nibble = (0x00000FF0L & v) >> 1 * 4;
        // clang-format on
        auto openssl_nibble_c = static_cast<char>(openssl_nibble);
        if (openssl_nibble) {
            openssl_nibble_c += 'a' - 1;
        }

        auto openssl_version = fmt::format("{}.{}.{}{}", openssl_major, openssl_minor, openssl_patch, openssl_nibble_c);
        auto &mdbx_v = ::mdbx_version;
        auto mdbx_version = fmt::format("{}.{}.{}_{}", mdbx_v.major, mdbx_v.minor, mdbx_v.patch, mdbx_v.tweak);
        auto app_version = fmt::format("{} {}", constants::client_name, constants::client_version);
        auto fltk_version = fmt::format("{}", Fl::version());
        auto ares_version = std::string(utils::cares_version());
        auto boost_version = []() {
            auto maj = BOOST_VERSION / 100000;
            auto min = (BOOST_VERSION / 100 % 1000);
            auto patch = (BOOST_VERSION % 100);
            return fmt::format("{}.{}.{}", maj, min, patch);
        }();

        device_id_short_cell = new static_string_provider_t();
        device_id_cell = new static_string_provider_t();
        uptime_cell = new static_string_provider_t();
        rx_cell = new static_string_provider_t();
        tx_cell = new static_string_provider_t();
        mdbx_entries_cell = new static_string_provider_t();
        mdbx_pages_cell = new static_string_provider_t();
        mdbx_size_cell = new static_string_provider_t();

        auto data = table_rows_t();
        data.push_back({"device id (short)", device_id_short_cell});
        data.push_back({"device id", device_id_cell});
        data.push_back({"uptime", uptime_cell});
        data.push_back({"received", rx_cell});
        data.push_back({"send", tx_cell});
        data.push_back({"mdbx entries", mdbx_entries_cell});
        data.push_back({"mdbx pages", mdbx_pages_cell});
        data.push_back({"mdbx size", mdbx_size_cell});
        data.push_back({"app version", new static_string_provider_t(app_version)});
        data.push_back({"mdbx version", new static_string_provider_t(mdbx_version)});
        data.push_back({"boost version", new static_string_provider_t(boost_version)});
        data.push_back({"lz4 version", new static_string_provider_t(LZ4_versionString())});
        data.push_back({"openssl version", new static_string_provider_t(openssl_version)});
        data.push_back({"fltk version", new static_string_provider_t(fltk_version)});
        data.push_back({"ares version", new static_string_provider_t(ares_version)});
        data.push_back({"", new static_string_provider_t("")});
        data.push_back({"actions", make_actions(*this)});

        assign_rows(std::move(data));

        update_db_info();
        Fl::add_timeout(1.0, on_uptime_timeout, this);
        Fl::add_timeout(10.0, on_db_refresh_timeout, this);
        refresh();
    }

    void refresh() override {
        auto sup = owner->get_supervisor();
        if (!sup) {
            return;
        }
        auto cluster = sup->get_cluster();
        auto rx_bytes = std::int64_t{0};
        auto tx_bytes = std::int64_t{0};

        auto device_id_short = std::string_view("XXXXXXX");
        auto device_id = std::string_view("XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX-XXXXXXX");
        if (cluster) {
            auto &self = cluster->get_device();
            auto &id = self->device_id();
            device_id_short = id.get_short();
            device_id = id.get_value();
            rx_bytes = static_cast<std::int64_t>(self->get_rx_bytes());
            tx_bytes = static_cast<std::int64_t>(self->get_tx_bytes());
        }
        auto pages = db_info.leaf_pages + db_info.ms_branch_pages + db_info.overflow_pages;
        auto size = get_file_size(pages * db_info.page_size);

        device_id_short_cell->update(device_id_short);
        device_id_cell->update(device_id);
        uptime_cell->update(sup->get_uptime());
        rx_cell->update(get_file_size(rx_bytes));
        tx_cell->update(get_file_size(tx_bytes));
        mdbx_entries_cell->update(fmt::format("{}", db_info.entries));
        mdbx_pages_cell->update(fmt::format("{}", pages));
        mdbx_size_cell->update(fmt::format("{}", size));
        redraw();
    }

    void view(const net::payload::db_info_response_t &res) override {
        db_info = res;
        refresh();
    }

    ~self_table_t() {
        db_info_guard.reset();
        Fl::remove_timeout(on_uptime_timeout, this);
        Fl::remove_timeout(on_db_refresh_timeout, this);
    }

    void update_db_info() {
        if (auto sup = owner->get_supervisor(); sup) {
            db_info_guard = sup->request_db_info(this);
        }
    }

    void on_restart() { owner->get_supervisor()->soft_restart(); }

    void on_regenerate_keys() {
        auto r = fl_choice("Are you sure? (no files on disk are touched)", "Yes", "No", nullptr);
        if (r != 0) {
            return;
        }

        auto sup = owner->get_supervisor();
        auto &cfg = sup->get_app_config();
        auto &cert_path = cfg.cert_file;
        auto &key_path = cfg.key_file;

        auto logger = sup->get_logger();
        auto cert_path_str = boost::nowide::narrow(cert_path.wstring());
        auto key_path_str = boost::nowide::narrow(key_path.wstring());
        auto pair = utils::generate_pair(constants::issuer_name);
        if (!pair) {
            logger->error("cannot generate cryptographic keys :: {}", pair.error().message());
            return;
        }
        auto &keys = pair.value();
        auto save_result = keys.save(cert_path_str.c_str(), key_path_str.c_str());
        if (!save_result) {
            logger->error("cannot store cryptographic keys ({} & {}) :: {}", cert_path_str, key_path_str,
                          save_result.error().message());
        }
        logger->info("keys has been regenerated, please restart");

        for (int i = 0; i < tree_node->children(); ++i) {
            auto child = tree_node->child(i);
            if (auto settings = dynamic_cast<settings_t *>(child); settings) {
                settings->update_label(true);
            }
        }
    }

    main_window_t *owner;
    db_info_viewer_guard_t db_info_guard;
    self_device_t *tree_node;
    db_info_t db_info;
    static_string_provider_ptr_t device_id_short_cell;
    static_string_provider_ptr_t device_id_cell;
    static_string_provider_ptr_t uptime_cell;
    static_string_provider_ptr_t rx_cell;
    static_string_provider_ptr_t tx_cell;
    static_string_provider_ptr_t mdbx_entries_cell;
    static_string_provider_ptr_t mdbx_pages_cell;
    static_string_provider_ptr_t mdbx_size_cell;
};

static void on_uptime_timeout(void *data) {
    auto self = reinterpret_cast<self_table_t *>(data);
    self->refresh();
    Fl::repeat_timeout(1.0, on_uptime_timeout, data);
}

static void on_db_refresh_timeout(void *data) {
    auto self = reinterpret_cast<self_table_t *>(data);
    self->update_db_info();
    Fl::repeat_timeout(10.0, on_db_refresh_timeout, data);
}

static auto make_actions(self_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto &container = static_cast<self_table_t &>(this->container);

            auto yy = y + padding, ww = 200, hh = h - padding * 2;

            auto restart = new Fl_Button(x + padding, yy, ww, hh, "restart");
            restart->callback([](auto, void *data) { static_cast<self_table_t *>(data)->on_restart(); }, &container);

            int xx = restart->x() + ww + padding * 2;

            auto regenerate = new Fl_Button(xx, yy, ww, hh, "regenerate keys");
            regenerate->callback([](auto, void *data) { static_cast<self_table_t *>(data)->on_regenerate_keys(); },
                                 &container);
            regenerate->color(FL_RED);
            xx = regenerate->x() + ww + padding * 2;

            group->resizable(nullptr);
            group->end();
            widget = group;

            this->reset();
            return widget;
        }
    };

    return new widget_t(container);
}

} // namespace

self_device_t::self_device_t(model::device_t &, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree) {
    add(prefs(), "settings", new settings_t(supervisor, tree));
    update_label();
}

void self_device_t::update_label() {
    auto self = supervisor.get_cluster()->get_device();
    auto device_id = self->device_id().get_short();
    auto label = fmt::format("(self) {}, {}", supervisor.get_app_config().device_name, device_id);
    this->label(label.data());
}

bool self_device_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        struct tile_t : contentable_t<Fl_Tile> {
            using parent_t = contentable_t<Fl_Tile>;
            using parent_t::parent_t;
        };

        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        int bot_h = 100;

        // auto group = new Fl_Group(x, y, w, h);
        auto group = new tile_t(x, y, w, h);
        auto resizable_area = new Fl_Box(x + w * 1. / 6, y + h * 1. / 2, w * 4. / 6, h / 2. - bot_h);
        group->resizable(resizable_area);

        group->begin();
        auto top = new self_table_t(supervisor.get_main_window(), this, x, y, w, h - bot_h);
        auto bot = [&]() -> Fl_Widget * {
            auto &device = supervisor.get_cluster()->get_device()->device_id();
            return new qr_button_t(device, supervisor, x, y + top->h(), w, bot_h);
        }();
        group->add(top->get_widget());
        group->add(bot);
        group->end();
        return group;
    });
    return true;
}
