#include "folder_table.h"

#include "model/diff/modify/remove_folder.h"
#include "model/diff/modify/remove_blocks.h"
#include "model/diff/modify/unshare_folder.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/local/scan_request.h"

#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include "../table_widget/int_input.h"
#include "../table_widget/label.h"
#include "../table_widget/path.h"

#include <FL/fl_ask.H>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

static constexpr int padding = 2;

namespace {

using ctx_t = folder_table_t::serialiazation_context_t;

struct device_share_widget_t final : widgetable_t {
    using parent_t = widgetable_t;
    device_share_widget_t(folder_table_t &container, model::device_ptr_t device_, bool disabled_);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void reset() override;
    bool store(void *data) override;

    model::device_ptr_t initial_device;
    model::device_ptr_t device;
    Fl_Choice *input;
    bool disabled;
};

device_share_widget_t::device_share_widget_t(folder_table_t &container, model::device_ptr_t device_, bool disabled_)
    : parent_t(container), initial_device{device_}, device{device_}, input{nullptr}, disabled{disabled_} {}

Fl_Widget *device_share_widget_t::create_widget(int x, int y, int w, int h) {
    auto group = new Fl_Group(x, y, w, h);
    group->begin();
    group->box(FL_FLAT_BOX);
    auto yy = y + padding, ww = w - padding * 2, hh = h - padding * 2;
    ww = std::min(300, ww);

    input = new Fl_Choice(x + padding, yy, ww, hh);
    auto add = new Fl_Button(input->x() + input->w() + padding * 2, yy, hh, hh, "@+");
    auto remove = new Fl_Button(add->x() + add->w() + padding * 2, yy, hh, hh, "@undo");

    add->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<folder_table_t *>(&self->container);
            table->on_add_share(*self);
        },
        this);
    remove->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<folder_table_t *>(&self->container);
            self->device = {};
            bool ok = table->on_remove_share(*self, self->device, self->initial_device);
            if (!ok) {
                self->input->value(0);
            }
        },
        this);
    input->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto table = static_cast<folder_table_t *>(&self->container);
            auto previous = self->device;
            if (self->input->value()) {
                auto cluster = table->container.supervisor.get_cluster();
                for (auto &it : cluster->get_devices()) {
                    auto device = it.item.get();
                    if (device == cluster->get_device().get()) {
                        continue;
                    }
                    auto short_id = device->device_id().get_short();
                    auto label = fmt::format("{}, {}", device->get_name(), short_id);
                    if (label == self->input->text()) {
                        self->device = it.item;
                        break;
                    }
                }
            } else {
                self->device = {};
            }
            table->on_select(self->device, previous);
        },
        this);

    group->end();
    group->resizable(nullptr);
    widget = group;
    reset();
    return widget;
}

void device_share_widget_t::reset() {
    auto &table = static_cast<folder_table_t &>(this->container);
    auto cluster = table.container.supervisor.get_cluster();

    input->add("(empty)");
    int i = 1;
    int index = i;
    for (auto &it : cluster->get_devices()) {
        auto &device = it.item;
        if (device == cluster->get_device()) {
            continue;
        }
        auto short_id = device->device_id().get_short();
        auto label = fmt::format("{}, {}", device->get_name(), short_id);
        input->add(label.data());
        if (device.get() == initial_device.get()) {
            this->device = device;
            index = i;
        }
        ++i;
    }
    input->value(this->device ? index : 0);
    if (disabled) {
        widget->deactivate();
    }
}

bool device_share_widget_t::store(void *data) {
    if (device) {
        auto ctx = reinterpret_cast<ctx_t *>(data);
        ctx->shared_with.put(device);
    }
    return true;
}

struct checkbox_widget_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;
    using parent_t::parent_t;

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
        return r;
    }
};

} // namespace

auto folder_table_t::make_title(folder_table_t &container, std::string_view title) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;

        widget_t(Fl_Widget &container, std::string_view title_) : parent_t(container), title{title_} {}

        void reset() override { input->label(title.data()); }

        std::string_view title;
    };
    return new widget_t(container, title);
}

auto folder_table_t::make_path(folder_table_t &container, bool disabled) -> widgetable_ptr_t {
    struct widget_t final : table_widget::path_t {
        using parent_t = table_widget::path_t;
        widget_t(Fl_Widget &container, std::string title, bool disabled_)
            : parent_t{container, title}, disabled{disabled_} {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            auto &container = static_cast<folder_table_t &>(this->container);
            if (disabled) {
                widget->deactivate();
            }
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto path = container.description.get_folder()->get_path();
            auto value = path.string();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_path(input->value());
            return true;
        }

        bool disabled;
    };
    return new widget_t(container, "folder directory", disabled);
}

auto folder_table_t::make_id(folder_table_t &container, bool disabled) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            auto &container = static_cast<folder_table_t &>(this->container);
            if (disabled) {
                widget->deactivate();
            }
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_folder()->get_id();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_id(input->value());
            return true;
        }

        bool disabled;
    };
    return new widget_t(container, disabled);
}

auto folder_table_t::make_label(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::input_t {
        using parent_t = table_widget::input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_folder()->get_label();
            input->value(value.data());
        }

        bool store(void *data) override {
            auto label = std::string(input->value());
            if (label.empty()) {
                auto &container = static_cast<folder_table_t &>(this->container);
                container.error = "label cannot be empty";
                return false;
            }

            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_label(label);
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_folder_type(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("Send and Receive");
            input->add("Send only");
            input->add("Receive only");
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_folder()->get_folder_type();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value = (db::FolderType)(input->value());

            ctx->folder.set_folder_type(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_pull_order(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::choice_t {
        using parent_t = table_widget::choice_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->size(200, r->h());
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            input->add("random");
            input->add("alphabetic");
            input->add("smallest first");
            input->add("largest first");
            input->add("oldest first");
            input->add("newest first");
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_folder()->get_pull_order();
            input->value(static_cast<int>(value));
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value = (db::PullOrder)(input->value());

            ctx->folder.set_pull_order(value);
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_index(folder_table_t &container, bool disabled) -> widgetable_ptr_t {
    struct widget_t final : table_widget::int_input_t {
        using parent_t = table_widget::int_input_t;
        widget_t(Fl_Widget &container, bool disabled_) : parent_t{container}, disabled{disabled_} {}

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            auto &container = static_cast<folder_table_t &>(this->container);
            if (disabled) {
                widget->deactivate();
            }
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_index();
            auto value_str = fmt::format("0x{:x}", value);
            input->value(value_str.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value_str = std::string_view(input->value());
            std::uint64_t value = 0;
            auto b = value_str.begin();
            if (value_str.size() >= 2 && value_str.starts_with("0x")) {
                b += 2;
            }
            auto result = std::from_chars(b, value_str.end(), value, 16);
            if (result.ec != std::errc() || value <= 0) {
                auto &container = static_cast<folder_table_t &>(this->container);
                container.error = "invalid index";
                return false;
            }
            ctx->index = value;
            return true;
        }

        bool disabled;
    };
    return new widget_t(container, disabled);
}

auto folder_table_t::make_read_only(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.description.get_folder()->is_read_only());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_read_only(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_rescan_interval(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::int_input_t {
        using parent_t = table_widget::int_input_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto r = parent_t::create_widget(x, y, w, h);
            input->callback([](auto, void *data) { reinterpret_cast<folder_table_t *>(data)->refresh(); }, &container);
            input->when(input->when() | FL_WHEN_CHANGED);
            return r;
        }

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            auto value = container.description.get_folder()->get_rescan_interval();
            auto value_str = std::to_string(value);
            input->value(value_str.data());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            auto value_str = std::string_view(input->value());
            int value = 0;
            auto result = std::from_chars(value_str.begin(), value_str.end(), value);
            if (result.ec != std::errc() || value <= 0) {
                auto &container = static_cast<folder_table_t &>(this->container);
                container.error = "invalid rescan interval";
                return false;
            }

            ctx->folder.set_rescan_interval(static_cast<std::uint32_t>(value));
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_ignore_permissions(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.description.get_folder()->are_permissions_ignored());
        }

        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_ignore_permissions(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_ignore_delete(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.description.get_folder()->is_deletion_ignored());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_ignore_delete(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_disable_tmp(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.description.get_folder()->are_temp_indixes_disabled());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_disable_temp_indexes(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_paused(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : checkbox_widget_t {
        using parent_t = checkbox_widget_t;
        using parent_t::parent_t;

        void reset() override {
            auto &container = static_cast<folder_table_t &>(this->container);
            input->value(container.description.get_folder()->is_paused());
        }
        bool store(void *data) override {
            auto ctx = reinterpret_cast<ctx_t *>(data);
            ctx->folder.set_paused(input->value());
            return true;
        }
    };
    return new widget_t(container);
}

auto folder_table_t::make_shared_with(folder_table_t &container, model::device_ptr_t device, bool disabled)
    -> widgetable_ptr_t {
    return new device_share_widget_t(container, device, disabled);
}

auto folder_table_t::make_notice(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;
        using parent_t::parent_t;

        void reset() override {
            auto &label = static_cast<folder_table_t &>(container).error;
            input->label(label.c_str());
        }
    };
    return new widget_t(container);
}

folder_table_t::folder_table_t(tree_item_t &container_, const model::folder_info_t &description_, int x, int y, int w,
                               int h)
    : parent_t(x, y, w, h), container{container_}, description{description_}, apply_button{nullptr},
      share_button{nullptr}, reset_button{nullptr} {

    shared_with.reset(new model::devices_map_t{});
    non_shared_with.reset(new model::devices_map_t{});

    auto folder = description.get_folder();
    auto cluster = folder->get_cluster();
    for (auto it : cluster->get_devices()) {
        auto &device = it.item;
        if (device != cluster->get_device()) {
            if (folder->is_shared_with(*device)) {
                shared_with->put(device);
            } else {
                non_shared_with->put(device);
            }
        }
    }
}

bool folder_table_t::on_remove_share(widgetable_t &widget, model::device_ptr_t device, model::device_ptr_t initial) {
    bool removed = false;
    auto [_, count] = scan(widget);
    if (count > 1 && !initial) {
        parent_t::remove_row(widget);
        removed = (bool)device;
    } else {
        redraw();
    }

    if (device) {
        shared_with->remove(device);
        non_shared_with->put(device);
    }

    container.refresh_content();
    return removed;
}

void folder_table_t::on_select(model::device_ptr_t device, model::device_ptr_t previous) {
    if (previous) {
        shared_with->remove(previous);
        non_shared_with->put(previous);
    }
    if (device) {
        shared_with->put(device);
        non_shared_with->remove(device);
    }
    container.refresh_content();
}

void folder_table_t::on_add_share(widgetable_t &widget) {
    auto [from_index, count] = scan(widget);
    if (count < container.supervisor.get_cluster()->get_devices().size() - 1) {
        assert(from_index);
        auto w = widgetable_ptr_t{};
        w.reset(new device_share_widget_t(*this, {}, false));
        insert_row("shared with", w, from_index + 1);
        refresh();
    }
}

std::pair<int, int> folder_table_t::scan(widgetable_t &widget) {
    auto &rows = get_rows();
    auto from_index = int{-1};
    int count = 0;
    for (int i = 0; i < rows.size(); ++i) {
        auto item = std::get_if<widgetable_ptr_t>(&rows[i].value);
        if (!item) {
            continue;
        }
        if (!dynamic_cast<device_share_widget_t *>(item->get())) {
            continue;
        }
        ++count;
        if (item->get() == &widget) {
            from_index = i;
        }
    }
    return {from_index, count};
}

void folder_table_t::on_share() {
    serialiazation_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &folder = ctx.folder;
    auto &sup = container.supervisor;
    auto log = sup.get_logger();
    auto &peer = ctx.shared_with.begin()->item;
    log->info("going to create folder {}({}) & share it with {}", folder.label(), folder.id(), peer->get_name());
    auto peer_id = peer->device_id().get_sha256();
    auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder, 0);
    if (!opt) {
        log->error("cannot create folder: {}", opt.assume_error().message());
        return;
    }

    auto cb = sup.call_share_folders(folder.id(), {std::string(peer_id)});
    sup.send_model<model::payload::model_update_t>(opt.assume_value(), cb.get());
}

void folder_table_t::on_apply() {
    serialiazation_context_t ctx;
    auto valid = store(&ctx);
    if (!valid) {
        return;
    }

    auto &sup = container.supervisor;
    auto &folder_db = ctx.folder;
    auto log = sup.get_logger();
    auto &cluster = *sup.get_cluster();

    auto opt = modify::upsert_folder_t::create(*sup.get_cluster(), sup.get_sequencer(), folder_db, ctx.index);
    if (!opt) {
        log->error("cannot create folder: {}", opt.assume_error().message());
        return;
    }
    auto &diff = opt.value();
    auto current = diff.get();

    if (initially_shared_with.size()) {
        auto folder = description.get_folder();
        auto orphaned_blocks = model::orphaned_blocks_t{};
        auto &folder_infos = folder->get_folder_infos();
        for (auto it : initially_shared_with) {
            auto &device = it.item;
            if (!ctx.shared_with.by_sha256(device->device_id().get_sha256())) {
                auto folder_info = folder_infos.by_device(*device);
                if (folder_info) {
                    log->info("going to unshare folder '{}' with {}({})", folder->get_label(), device->get_name(),
                              device->device_id().get_short());
                    auto sub_diff = model::diff::cluster_diff_ptr_t{};
                    sub_diff = new modify::unshare_folder_t(cluster, *folder_info, &orphaned_blocks);
                    current = current->assign_sibling(sub_diff.get());
                }
            }
        }
        if (auto orphaned_set = orphaned_blocks.deduce(); orphaned_set.size()) {
            log->info("going to remove {} orphaned blocks", orphaned_set.size());
            auto sub_diff = model::diff::cluster_diff_ptr_t{};
            sub_diff = new modify::remove_blocks_t(std::move(orphaned_set));
            current = current->assign_sibling(sub_diff.get());
        }
    }

    auto devices = std::vector<std::string>{};
    for (auto it : initially_non_shared_with) {
        auto &device = it.item;
        auto sha256 = device->device_id().get_sha256();
        if (ctx.shared_with.by_sha256(sha256)) {
            devices.emplace_back(std::string(sha256));
        }
    }

    auto &folder_id = folder_db.id();
    auto cb = devices.empty() ? sup.call_select_folder(std::move(folder_id))
                              : sup.call_share_folders(std::move(folder_id), std::move(devices));
    sup.send_model<model::payload::model_update_t>(diff, cb.get());
}

void folder_table_t::on_reset() {
    auto &rows = get_rows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        auto &row = rows[i];
        auto widget = std::get_if<widgetable_ptr_t>(&row.value);
        if (!widget) {
            continue;
        }
        auto share = dynamic_cast<device_share_widget_t *>(widget->get());
        if (!share) {
            continue;
        }
        bool remove = false;
        auto &device = share->initial_device;
        if (!device) {
            remove = true;
        } else {
            auto device_id = device->device_id().get_sha256();
            remove = !initially_shared_with.by_sha256(device_id);
        }
        if (remove) {
            remove_row(**widget);
            --i;
        }
    }
    reset();
    refresh();
}

void folder_table_t::on_remove() {
    auto r = fl_choice("Are you sure? (no files on disk are touched)", "Yes", "No", nullptr);
    if (r != 0) {
        return;
    }
    auto &sup = container.supervisor;
    auto &cluster = *sup.get_cluster();
    auto &sequencer = sup.get_sequencer();
    auto diff = model::diff::cluster_diff_ptr_t{};
    diff = new model::diff::modify::remove_folder_t(cluster, sequencer, *description.get_folder());
    sup.send_model<model::payload::model_update_t>(std::move(diff), this);
}

void folder_table_t::on_rescan() {
    auto &sup = container.supervisor;
    auto &cluster = *sup.get_cluster();
    auto diff = model::diff::cluster_diff_ptr_t{};
    auto folder_id = description.get_folder()->get_id();
    diff = new model::diff::local::scan_request_t(folder_id);
    sup.send_model<model::payload::model_update_t>(std::move(diff), this);
}
