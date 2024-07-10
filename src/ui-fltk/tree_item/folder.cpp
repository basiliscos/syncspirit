#include "folder.h"

#include "../table_widget/checkbox.h"
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

struct checkbox_widget_t final : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;

    checkbox_widget_t(folder_t &container, bool value_) : parent_t(container), value{value_} {}

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto r = parent_t::create_widget(x, y, w, h);
        input->deactivate();
        input->value(value ? 1 : 0);
        return r;
    }

    bool value;
};

inline auto static make_checkbox(folder_t &container, bool value) -> widgetable_ptr_t {
    return new checkbox_widget_t(container, value);
}

} // namespace

folder_t::folder_t(model::folder_info_t &folder_info_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, true), folder_info{folder_info_} {
    update_label();
}

void folder_t::update_label() {
    auto f = folder_info.get_folder();
    auto value = fmt::format("{}, {}", f->get_label(), f->get_id());
    label(value.data());
    tree()->redraw();
}

bool folder_t::on_select() {
    supervisor.replace_content([&](Fl_Widget *prev) -> Fl_Widget * {
        auto data = table_rows_t();
        auto f = folder_info.get_folder();
        auto entries = folder_info.get_file_infos().size();
        data.push_back({"path", f->get_path().string()});
        data.push_back({"id", std::string(f->get_id())});
        data.push_back({"label", std::string(f->get_label())});
        data.push_back({"entries", std::to_string(entries)});
        data.push_back({"index", std::to_string(folder_info.get_index())});
        data.push_back({"max sequence", std::to_string(folder_info.get_max_sequence())});
        data.push_back({"read only", make_checkbox(*this, f->is_read_only())});
        data.push_back({"ignore permissions", make_checkbox(*this, f->are_permissions_ignored())});
        data.push_back({"ignore delete", make_checkbox(*this, f->is_deletion_ignored())});
        data.push_back({"disable temp indixes", make_checkbox(*this, f->are_temp_indixes_disabled())});
        data.push_back({"paused", make_checkbox(*this, f->is_paused())});

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto content = new static_table_t(std::move(data), x, y, w, h);
        return content;
    });
    return true;
}
