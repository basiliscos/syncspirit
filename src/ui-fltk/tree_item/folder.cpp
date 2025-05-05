// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "folder.h"
#include "../symbols.h"
#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include "../table_widget/label.h"
#include "../content/folder_table.h"
#include "../utils.hpp"
#include "presentation/folder_presence.h"
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <spdlog/fmt/fmt.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#include <malloc.h>
#else
#include <alloca.h>
#endif

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

folder_t::folder_t(presentation::folder_presence_t &presence, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(presence, supervisor, tree) {
    auto cluster = supervisor.get_cluster();
    update_label();
    populate_dummy_child();
}

#if 0
void folder_t::update_label() {
    auto &folder_presence = static_cast<presentation::folder_presence_t &>(presence);
    auto &stats = folder_presence.get_stats();
    auto &folder_info = folder_presence.get_folder_info();
    auto &folder = *folder_info.get_folder();
    auto id = folder.get_id();
    auto folder_label = folder.get_label();
    char scanning_buff[32];
    char synchronizing_buff[32];
    auto scanning = std::string_view();
    auto synchronizing = std::string_view();
#if 0
    if (folder.is_scanning()) {
        auto share = (stats.entries) ? 100.0 * stats.scanned_entries / stats.entries : 0;
        auto eob = fmt::format_to(scanning_buff, " ({} {}%)", symbols::scanning, (int)share);
        scanning = std::string_view(scanning_buff, eob);
        color_context = color_context_t::outdated;
    }
    if (folder.is_synchronizing()) {
        auto eob = fmt::format_to(synchronizing_buff, " {}", symbols::synchronizing);
        synchronizing = std::string_view(synchronizing_buff, eob);
    }
#endif
    auto sz = folder_label.size() + id.size() + synchronizing.size() + scanning.size() + 16;
    auto buff = (char *)alloca(sz);
    auto eob = fmt::format_to(buff, "{}, {}{}{}", folder_label, id, synchronizing, scanning);
    *eob = 0;
    labelfgcolor(get_color());
    label(buff);
}
#endif

static constexpr int padding = 2;

namespace {

using folder_table_t = content::folder_table_t;

static auto make_actions(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : widgetable_t {
        using parent_t = widgetable_t;
        using parent_t::parent_t;

        Fl_Widget *create_widget(int x, int y, int w, int h) override {
            auto group = new Fl_Group(x, y, w, h);
            group->begin();
            group->box(FL_FLAT_BOX);
            auto &container = static_cast<folder_table_t &>(this->container);

            auto yy = y + padding, ww = 100, hh = h - padding * 2;

            auto apply = new Fl_Button(x + padding, yy, ww, hh, "apply");
            apply->deactivate();
            apply->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_apply(); }, &container);
            container.apply_button = apply;
            int xx = apply->x() + ww + padding * 2;

            auto reset = new Fl_Button(xx, yy, ww, hh, "reset");
            reset->deactivate();
            reset->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_reset(); }, &container);
            container.reset_button = reset;
            xx = reset->x() + ww + padding * 2;

            auto rescan = new Fl_Button(xx, yy, ww, hh, "rescan");
            rescan->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_rescan(); }, &container);
            rescan->deactivate();
            container.rescan_button = rescan;
            xx = rescan->x() + ww + padding * 2;

            auto remove = new Fl_Button(xx, yy, ww, hh, "remove");
            remove->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_remove(); }, &container);
            remove->color(FL_RED);
            xx = remove->x() + ww + padding * 2;

            group->resizable(nullptr);
            group->end();
            widget = group;

            this->reset();
            return widget;
        }
    };

    return new widget_t(container);
}

struct table_t : content::folder_table_t {
    using parent_t = content::folder_table_t;
    using parent_t::parent_t;

    table_t(tree_item_t &container_, const model::folder_info_t &description, int x, int y, int w, int h)
        : parent_t(container_, description, x, y, w, h) {

        entries_cell = new static_string_provider_t();
        entries_size_cell = new static_string_provider_t();
        max_sequence_cell = new static_string_provider_t();
        scan_start_cell = new static_string_provider_t();
        scan_finish_cell = new static_string_provider_t();

        auto data = table_rows_t();
        data.push_back({"", make_title(*this, "edit existing folder")});
        data.push_back({"path", make_path(*this, true)});
        data.push_back({"id", make_id(*this, true)});
        data.push_back({"label", make_label(*this, false)});
        data.push_back({"type", make_folder_type(*this, false)});
        data.push_back({"pull order", make_pull_order(*this, false)});
        data.push_back({"entries", entries_cell});
        data.push_back({"entries size", entries_size_cell});
        data.push_back({"max sequence", max_sequence_cell});
        data.push_back({"index", make_index(*this, true)});
        data.push_back({"scan start", scan_start_cell});
        data.push_back({"scan finish", scan_finish_cell});
        data.push_back({"rescan interval", make_rescan_interval(*this, false)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this, false)});
        data.push_back({"ignore delete", make_ignore_delete(*this, false)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this, false)});
        data.push_back({"scheduled", make_scheduled(*this, false)});
        data.push_back({"paused", make_paused(*this, false)});

        int shared_count = 0;
        for (auto it : *shared_with) {
            auto &device = it.item;
            auto widget = make_shared_with(*this, device, false);
            data.push_back({"shared_with", widget});
            ++shared_count;
        }
        if (!shared_count) {
            auto widget = make_shared_with(*this, {}, false);
            data.push_back({"shared_with", widget});
        }
        data.push_back({"", notice = make_notice(*this)});
        data.push_back({"actions", make_actions(*this)});

        initially_shared_with = *shared_with;
        initially_non_shared_with = *non_shared_with;
        assign_rows(std::move(data));

        refresh();
    }

    void refresh() override {
        auto &folder_item = static_cast<folder_t &>(container);
        auto &fp = static_cast<presentation::folder_presence_t &>(folder_item.get_presence());
        auto &folder_info = fp.get_folder_info();
        serialization_context_t ctx;
        description.get_folder()->serialize(ctx.folder);

        auto copy_data = db::encode(ctx.folder);
        error = {};
        auto valid = store(&ctx);

        // clang-format off
        auto is_same = (copy_data == db::encode(ctx.folder))
                    && (initially_shared_with == ctx.shared_with);
        // clang-format on
        if (!is_same) {
            if (valid) {
                apply_button->activate();
            }
            reset_button->activate();
            rescan_button->deactivate();
        } else {
            apply_button->deactivate();
            auto &folders = container.supervisor.get_cluster()->get_folders();
            auto folder_id = db::get_id(ctx.folder);
            auto folder = folders.by_id(folder_id);
            if (!folder->is_scanning()) {
                rescan_button->activate();
            }
            reset_button->deactivate();
        }

        auto folder = description.get_folder();
        auto &date_start = folder->get_scan_start();
        auto &date_finish = folder->get_scan_finish();
        auto scan_start = date_start.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_start);
        auto scan_finish = date_finish.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_finish);
        scan_start_cell->update(scan_start);
        scan_finish_cell->update(scan_finish);

        auto max_sequence = description.get_max_sequence();
        auto &stats = fp.get_stats();
        entries_cell->update(fmt::format("{}", stats.entities));
        entries_size_cell->update(get_file_size(stats.size));
        max_sequence_cell->update(fmt::format("{}", max_sequence));

        notice->reset();
        redraw();
    }
};

} // namespace

bool folder_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using devices_ptr_t = table_t::shared_devices_t;
        auto prev = content->get_widget();
        auto shared_with = devices_ptr_t(new model::devices_map_t{});
        auto non_shared_with = devices_ptr_t(new model::devices_map_t{});

        auto &fp = static_cast<presentation::folder_presence_t &>(presence);
        auto &folder_info = fp.get_folder_info();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, folder_info, x, y, w, h);
    });
    return true;
}

void folder_t::reset_stats() {
#if 0
    using queue_t = std::deque<augmentation_entry_base_t *>;
    auto queue = queue_t();
    auto root = static_cast<augmentation_entry_base_t *>(get_proxy().get());
    queue.push_back(root);
    while (!queue.empty()) {
        auto it = queue.front();
        queue.pop_front();
        it->reset_stats();
        for (auto &c : it->get_children()) {
            queue.emplace_back(c.get());
        }
    }
    std::abort();
#endif
}
