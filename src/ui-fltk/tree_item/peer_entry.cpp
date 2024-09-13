#include "peer_entry.h"
#include "../static_table.h"
#include "../table_widget/checkbox.h"
#include "spdlog/fmt/fmt.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

namespace {

struct ro_checkbox_t : table_widget::checkbox_t {
    using parent_t = table_widget::checkbox_t;

    ro_checkbox_t(Fl_Widget &container, bool value_) : parent_t(container), value{value_} {}

    Fl_Widget *create_widget(int x, int y, int w, int h) override {
        auto widget = parent_t::create_widget(x, y, w, h);
        input->deactivate();
        input->value(value ? 1 : 0);
        return widget;
    }

    bool value;
};

auto make_checkbox(Fl_Widget &container, bool value) -> widgetable_ptr_t { return new ro_checkbox_t(container, value); }

} // namespace

peer_entry_t::peer_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry_)
    : parent_t(supervisor, tree, true), entry{entry_} {
    entry.set_augmentation(get_proxy());
}

auto peer_entry_t::get_entry() -> model::file_info_t * { return &entry; }

bool peer_entry_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        struct my_table_t : static_table_t {
            using parent_t = static_table_t;
            my_table_t(peer_entry_t &container_, int x, int y, int w, int h)
                : parent_t(x, y, w, h), container{container_} {
                auto &entry = container.entry;
                auto data = table_rows_t();
                auto modified_s = entry.get_modified_s();
                auto modified_date = model::pt::from_time_t(modified_s);
                auto modified = model::pt::to_simple_string(modified_date);

                data.push_back({"name", std::string(entry.get_name())});
                data.push_back({"modified", modified});

                data.push_back({"size", std::to_string(entry.get_size())});
                data.push_back({"block size", std::to_string(entry.get_block_size())});
                data.push_back({"blocks", std::to_string(entry.get_blocks().size())});
                data.push_back({"permissions", fmt::format("0{:o}", entry.get_permissions())});
                data.push_back({"modified_s", fmt::format("{}", modified_s)});
                data.push_back({"modified_ns", fmt::format("{}", entry.get_modified_ns())});
                data.push_back({"modified_by", fmt::format("{}", entry.get_modified_by())});
                data.push_back({"is_link", make_checkbox(*this, entry.is_link())});
                data.push_back({"is_invalid", make_checkbox(*this, entry.is_invalid())});
                data.push_back({"no_permissions", make_checkbox(*this, entry.has_no_permissions())});
                data.push_back({"sequence", fmt::format("{}", entry.get_sequence())});
                data.push_back({"symlink_target", std::string(entry.get_link_target())});
                assign_rows(std::move(data));
            }

            peer_entry_t &container;
        };

        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new my_table_t(*this, x, y, w, h);
    });
    return true;
}
