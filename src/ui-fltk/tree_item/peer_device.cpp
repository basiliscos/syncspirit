#include "peer_device.h"
#include "unknown_folders.h"
#include "../qr_button.h"
#include "../table_widget/checkbox.h"
#include "model/diff/modify/remove_peer.h"
#include "model/diff/modify/update_peer.h"
#include "utils/format.hpp"

#include <boost/asio.hpp>
#include <vector>

#include <FL/Fl_Tile.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/fl_ask.H>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

static constexpr int padding = 2;

static std::string format_urls(const utils::uri_container_t &uris) { return fmt::format("{}", fmt::join(uris, ",")); }

namespace {

struct my_table_t;

static widgetable_ptr_t make_name(my_table_t &container);
static widgetable_ptr_t make_introducer(my_table_t &container);
static widgetable_ptr_t make_auto_accept(my_table_t &container);
static widgetable_ptr_t make_auto_accept(my_table_t &container);
static widgetable_ptr_t make_paused(my_table_t &container);
static widgetable_ptr_t make_compressions(my_table_t &container);
static widgetable_ptr_t make_addresses(my_table_t &container);
static widgetable_ptr_t make_actions(my_table_t &container);

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(peer_device_t &container_, int x, int y, int w, int h)
        : parent_t(x, y, w, h), container{container_}, apply_button{nullptr}, reset_button{nullptr} {
        auto data = table_rows_t();
        auto &peer = container.peer;
        auto &ep = peer.get_endpoint();

        auto device_id = peer.device_id().get_value();
        auto device_id_short = peer.device_id().get_short();
        auto cert_name = peer.get_cert_name();
        auto endpoint = ep.port() ? fmt::format("{}", ep) : "";
        auto last_seen = ep.port() ? "now" : model::pt::to_simple_string(peer.get_last_seen());

        data.push_back({"name", make_name(*this)});
        data.push_back({"last_seen", last_seen});
        data.push_back({"addresses", make_addresses(*this)});
        data.push_back({"endpoint", endpoint});
        data.push_back({"state", container.get_state()});
        data.push_back({"cert name", cert_name.value_or("")});
        data.push_back({"client name", std::string(peer.get_client_name())});
        data.push_back({"client version", std::string(peer.get_client_version())});
        data.push_back({"device id (short)", std::string(device_id_short)});
        data.push_back({"device id", device_id});
        data.push_back({"introducer", make_introducer(*this)});
        data.push_back({"auto accept", make_auto_accept(*this)});
        data.push_back({"paused", make_paused(*this)});
        data.push_back({"compression", make_compressions(*this)});
        data.push_back({"actions", make_actions(*this)});

        assign_rows(std::move(data));
    }

    void on_remove() {
        auto r = fl_choice("Are you sure?", "Yes", "No", nullptr);
        if (r != 0) {
            return;
        }
        auto &supervisor = container.supervisor;
        auto &cluster = *supervisor.get_cluster();
        auto diff = cluster_diff_ptr_t(new modify::remove_peer_t(cluster, container.peer));
        supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
    }

    void on_apply() {
        auto data = container.peer.serialize();
        auto device = db::Device();
        auto ok = device.ParseFromArray(data.data(), data.size());
        assert(ok);
        auto valid = store(&device);
        if (valid) {
            auto &supervisor = container.supervisor;
            auto &device_id = container.peer.device_id();
            auto &cluster = *supervisor.get_cluster();
            auto diff = cluster_diff_ptr_t(new modify::update_peer_t(std::move(device), device_id, cluster));
            supervisor.send_model<model::payload::model_update_t>(std::move(diff), this);
        }
    }

    void on_reset() {
        reset();
        refresh();
    }

    void refresh() override {
        auto &peer = container.peer;
        auto initial_data = peer.serialize();
        auto current = db::Device();
        auto ok = current.ParseFromArray(initial_data.data(), initial_data.size());
        assert(ok);
        auto valid = store(&current);

        auto current_data = current.SerializeAsString();
        if (initial_data != current_data) {
            if (valid) {
                apply_button->activate();
            }
            reset_button->activate();
        } else {
            apply_button->deactivate();
            reset_button->deactivate();
        }

        auto &rows = get_rows();
        for (size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].label == "state") {
                update_value(i, container.get_state());
            } else if (rows[i].label == "last_seen") {
                auto last_seen = peer.get_endpoint().port() ? "now" : model::pt::to_simple_string(peer.get_last_seen());
                update_value(i, last_seen);
            } else if (rows[i].label == "endpoint") {
                auto endpoint = peer.get_endpoint().port() ? fmt::format("{}", peer.get_endpoint()) : "";
                update_value(i, endpoint);
            } else if (rows[i].label == "client name") {
                update_value(i, std::string(peer.get_client_name()));
            } else if (rows[i].label == "client version") {
                update_value(i, std::string(peer.get_client_version()));
            }
        }
    }

    peer_device_t &container;
    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);
        return r;
    }
};

static widgetable_ptr_t make_actions(my_table_t &container) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = 100, hh = h - padding * 2;
            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            auto reset = new Fl_Button(apply->x() + ww + padding * 2, yy, ww, hh, "reset");
            auto remove = new Fl_Button(reset->x() + ww + padding * 2, yy, ww, hh, "remove");
            apply->deactivate();
            reset->deactivate();
            remove->color(FL_RED);
            group->end();
            widget = group;

            apply->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_apply(); }, &container);
            reset->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_reset(); }, &container);
            remove->callback([](auto, void *data) { static_cast<my_table_t *>(data)->on_remove(); }, &container);

            this->reset();
            auto &container = static_cast<my_table_t &>(this->container);
            container.apply_button = apply;
            container.reset_button = reset;
            return widget;
        }
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_name(my_table_t &container) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
            ww = std::min(300, ww);

            input = new Fl_Input(x + padding, yy, ww, hh);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);

            group->end();
            group->resizable(nullptr);
            widget = group;

            reset();
            return widget;
        }

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.container.peer.get_name().data());
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.set_name(input->value());
            return true;
        };

        mutable Fl_Input *input;
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_introducer(my_table_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.container.peer.is_introducer());
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.set_introducer(input->value());
            return true;
        };
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_auto_accept(my_table_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.container.peer.has_auto_accept());
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.set_auto_accept(input->value());
            return true;
        };
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_paused(my_table_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            input->value(container.container.peer.is_paused());
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.set_paused(input->value());
            return true;
        };
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_compressions(my_table_t &container) {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
            ww = std::min(150, ww);

            input = new Fl_Choice(x + padding, yy, ww, hh);
            input->add("metadata");
            input->add("never");
            input->add("always");
            input->value(0);
            input->callback([](auto, void *data) { reinterpret_cast<my_table_t *>(data)->refresh(); }, &container);

            group->end();
            group->resizable(nullptr);
            widget = group;

            reset();
            return widget;
        }

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            using C = proto::Compression;
            auto c = container.container.peer.get_compression();
            if (c == C::METADATA) {
                input->value(0);
            } else if (c == C::NEVER) {
                input->value(1);
            } else {
                input->value(2);
            }
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.set_compression(static_cast<proto::Compression>(input->value()));
            return true;
        };

        mutable Fl_Choice *input;
    };

    return new widget_t(container);
}

static widgetable_ptr_t make_addresses(my_table_t &container) {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
            ww = std::min(300, ww);
            input = new Fl_Input_Choice(x + padding, yy, ww, hh);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->callback([](auto, void *data) { reinterpret_cast<widget_t *>(data)->on_change(); }, this);
            input->tooltip("comma-separated urls like tcp://ip:port");

            group->end();
            widget = group;

            reset();
            return widget;
        }

        void reset() override {
            auto &container = static_cast<my_table_t &>(this->container);
            auto &uris = container.container.peer.get_static_uris();
            auto menu = input->menubutton();
            input->clear();
            menu->add("dynamic", 0, 0, 0, (uris.empty() ? FL_MENU_INACTIVE : 0));
            menu->add("static", 0, 0, 0, (uris.empty() ? 0 : FL_MENU_INACTIVE));
            auto items = menu->menu();

            if (uris.empty()) {
                menu->value(0);
                input->value("dynamic");
                input->input()->deactivate();
            } else {
                menu->value(1);
                input->input()->activate();
                auto value = format_urls(uris);
                input->value(value.data());
            }
        }

        bool store(void *data) override {
            auto &device = *reinterpret_cast<db::Device *>(data);
            device.clear_addresses();
            if (input->input()->active() == 0) {
                return true;
            } else {
                std::vector<std::string_view> addresses;
                std::string_view data = input->input()->value();
                auto left = data;
                while (left.size()) {
                    if (left.front() == ' ' || left.front() == ',') {
                        left = left.substr(1);
                        continue;
                    }
                    auto e = left.find_first_of(", ");
                    auto piece = left.substr(0, e);
                    auto url = utils::parse(piece);
                    if (url) {
                        addresses.push_back(piece);
                        if (e != left.npos) {
                            left = left.substr(piece.size() + 1);
                        } else {
                            left = {};
                        }
                    } else {
                        return false;
                    }
                }
                for (auto addr : addresses) {
                    *device.add_addresses() = addr;
                }
                return true;
            }
        };

        void on_change() {
            auto menu = input->menubutton();
            auto &container = static_cast<my_table_t &>(this->container);
            if (menu->changed()) {
                if (menu->value() == 0) {
                    input->value("dynamic");
                    menu->clear();
                    menu->add("dynamic", 0, 0, 0, FL_MENU_INACTIVE);
                    menu->add("static", 0, 0, 0, 0);
                    input->input()->deactivate();
                } else {
                    auto &uris = container.container.peer.get_static_uris();
                    auto value = format_urls(uris);
                    input->value(value.data());
                    input->input()->activate();
                    input->input()->take_focus();
                    menu->clear();
                    menu->add("dynamic", 0, 0, 0, 0);
                    menu->add("static", 0, 0, 0, FL_MENU_INACTIVE);
                }
            }
            container.refresh();
        }

        mutable Fl_Input_Choice *input;
    };
    return new widget_t(container);
}

} // namespace

peer_device_t::peer_device_t(model::device_t &peer_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), peer{peer_} {
    auto unknown_folders = new unknown_folders_t(peer, supervisor, tree);
    add(prefs(), unknown_folders->label(), unknown_folders);
    update_label();
}

void peer_device_t::update_label() {
    auto name = peer.get_name();
    auto id = peer.device_id().get_short();
    auto value = fmt::format("{}, {} [{}]", name, id, get_state());
    label(value.data());
    tree()->redraw();
}

bool peer_device_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        return new my_table_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
    });

    return true;
}

std::string peer_device_t::get_state() {
    switch (peer.get_state()) {
    case model::device_state_t::online:
        return "online";
    case model::device_state_t::discovering:
        return "discovering";
    case model::device_state_t::connecting:
        return "connecting";
    default:
        return "offline";
    }
}

tree_item_t *peer_device_t::get_unknown_folders() { return static_cast<tree_item_t *>(child(0)); }
