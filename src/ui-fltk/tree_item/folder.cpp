#include "folder.h"

#include "../symbols.h"
#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include "../table_widget/label.h"
#include "../content/folder_table.h"
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;


folder_t::folder_t(model::folder_t &folder, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, {}) {

    auto cluster = supervisor.get_cluster();
    auto local_folder = folder.get_folder_infos().by_device(*cluster->get_device()).get();
    augmentation = new augmentation_entry_root_t(*local_folder, this);
    local_folder->set_augmentation(get_proxy());

    update_label();

    add(prefs(), "[dummy]", new tree_item_t(supervisor, tree, false));
    tree->close(this, 0);
}

void folder_t::update_label() {
    auto entry = static_cast<augmentation_entry_root_t*>(augmentation.get());
    auto folder_info = entry->get_folder();
    auto &folder = *folder_info->get_folder();
    auto symbol = std::string_view();
    if (folder.is_scanning()) {
        symbol = symbols::scaning;
    }
    if (folder.is_synchronizing()) {
        symbol = symbols::syncrhonizing;
    }
    auto value = fmt::format("{}, {} {}", folder.get_label(), folder.get_id(), symbol);
    label(value.data());
}

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

            auto remove = new Fl_Button(rescan->x() + ww + padding * 2, yy, ww, hh, "remove");
            remove->callback([](auto, void *data) { static_cast<folder_table_t *>(data)->on_remove(); }, &container);
            remove->color(FL_RED);
            xx = remove->x() + ww + padding * 2;

            auto invisible = new Fl_Box(xx, yy, w - (xx - group->x() + padding * 2), hh);
            invisible->hide();
            group->resizable(invisible);
            group->end();
            widget = group;

            this->reset();
            return widget;
        }
    };

    return new widget_t(container);
}

static auto make_title(folder_table_t &container) -> widgetable_ptr_t {
    struct widget_t final : table_widget::label_t {
        using parent_t = table_widget::label_t;
        using parent_t::parent_t;

        void reset() override {
            auto text = std::string_view("");
            auto &container = static_cast<folder_table_t &>(this->container);
            text = "editing existing folder";
            input->label(text.data());
        }
    };
    return new widget_t(container);
}

struct table_t : content::folder_table_t {
    using parent_t = content::folder_table_t;
    using parent_t::parent_t;

    table_t(tree_item_t &container_, const model::folder_info_t &description, int x, int y, int w, int h)
        : parent_t(container_, description, x, y, w, h) {

        auto entries = description.get_file_infos().size();
        auto max_sequence = description.get_max_sequence();
        entries_cell = new static_string_provider_t();
        entries_size_cell = new static_string_provider_t();
        max_sequence_cell = new static_string_provider_t();
        scan_start_cell = new static_string_provider_t();
        scan_finish_cell = new static_string_provider_t();

        auto data = table_rows_t();
        data.push_back({"", make_title(*this, "edit existing folder")});
        data.push_back({"path", make_path(*this, true)});
        data.push_back({"id", make_id(*this, true)});
        data.push_back({"label", make_label(*this)});
        data.push_back({"type", make_folder_type(*this)});
        data.push_back({"pull order", make_pull_order(*this)});
        data.push_back({"entries", entries_cell});
        data.push_back({"entries size", entries_size_cell});
        data.push_back({"max sequence", max_sequence_cell});
        data.push_back({"index", make_index(*this, true)});
        data.push_back({"scan start", scan_start_cell});
        data.push_back({"scan finish", scan_finish_cell});
        data.push_back({"read only", make_read_only(*this)});
        data.push_back({"rescan interval", make_rescan_interval(*this)});
        data.push_back({"ignore permissions", make_ignore_permissions(*this)});
        data.push_back({"ignore delete", make_ignore_delete(*this)});
        data.push_back({"disable temp indixes", make_disable_tmp(*this)});
        data.push_back({"scheduled", make_scheduled(*this)});
        data.push_back({"paused", make_paused(*this)});

        auto cluster = container.supervisor.get_cluster();
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
        auto aug = static_cast<augmentation_entry_base_t*>(container.get_proxy().get());
        auto folder_info = aug->get_folder();
        serialiazation_context_t ctx;
        description.get_folder()->serialize(ctx.folder);

        auto copy_data = ctx.folder.SerializeAsString();
        error = {};
        auto valid = store(&ctx);

        // clang-format off
        auto is_same = (copy_data == ctx.folder.SerializeAsString())
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
            auto folder = folders.by_id(ctx.folder.id());
            if (!folder->is_scanning()) {
                rescan_button->activate();
            }
            reset_button->deactivate();
        }

        auto cluster = container.supervisor.get_cluster();
        auto folder = description.get_folder();
        auto &date_start = folder->get_scan_start();
        auto &date_finish = folder->get_scan_finish();
        auto scan_start = date_start.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_start);
        auto scan_finish = date_finish.is_not_a_date_time() ? "-" : model::pt::to_simple_string(date_finish);
        scan_start_cell->update(scan_start);
        scan_finish_cell->update(scan_finish);

        auto entries_size = std::size_t{0};
        for (auto &it : folder_info->get_file_infos()) {
            entries_size += it.item->get_size();
        }
        auto entries_count = description.get_file_infos().size();
        auto max_sequence = description.get_max_sequence();
        entries_cell->update(fmt::format("{}", entries_count));
        entries_size_cell->update(fmt::format("{}", entries_size));
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

        auto aug = static_cast<augmentation_entry_base_t*>(get_proxy().get());
        auto& folder_info = *aug->get_folder();

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, folder_info, x, y, w, h);
    });
    return true;
}
