#include "folder_table.h"

#if 0
#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::content;

static constexpr int padding = 2;

namespace {

struct device_share_widget_t final : widgetable_t {
    using parent_t = widgetable_t;
    device_share_widget_t(tree_item_t &container, model::device_ptr_t device_);

    Fl_Widget *create_widget(int x, int y, int w, int h) override;
    void reset() override;
    bool store(void *data) override;

    model::device_ptr_t initial_device;
    model::device_ptr_t device;
    Fl_Choice *input;
};

device_share_widget_t::device_share_widget_t(tree_item_t &container, model::device_ptr_t device_)
    : parent_t(container), initial_device{device_}, device{device_}, input{nullptr} {}

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
            auto table = static_cast<folder_table_t *>(self->container);
            table->on_add_share(*self);
        },
        this);
    remove->callback(
        [](auto, void *data) {
            auto self = reinterpret_cast<device_share_widget_t *>(data);
            auto &container = static_cast<folder_t &>(self->container);
            auto table = static_cast<folder_table_t *>(container.content);
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
            auto &container = static_cast<folder_t &>(self->container);
            auto table = static_cast<folder_table_t *>(container.content);
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
                        auto table = static_cast<folder_table_t *>(container.content);
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
    auto &container = static_cast<folder_table_t &>(this->container);
    auto cluster = container.supervisor.get_cluster();

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
    if (index == 1) {
        index = 0;
    }
    input->value(index);
}

bool device_share_widget_t::store(void *data) {
    if (device) {
        auto ctx = reinterpret_cast<serialiazation_context_t *>(data);
        ctx->shared_with.put(device);
    }
    return true;
}


}

folder_table_t::folder_table_t(tree_item_t &container_, shared_devices_t shared_with_, shared_devices_t non_shared_with_,
           table_rows_t &&rows, int x, int y, int w, int h)
    : parent_t(std::move(rows), x, y, w, h), container{container_}, shared_with{shared_with_},
      non_shared_with{std::move(non_shared_with_)} {
    initially_shared_with = *shared_with;
    initially_non_shared_with = *non_shared_with;
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
        w.reset(new device_share_widget_t(container, {}));
        insert_row("shared with", w, from_index + 1);
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
#endif
