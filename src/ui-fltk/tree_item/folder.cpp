#include "folder.h"

#include "../table_widget/checkbox.h"
#include "../table_widget/choice.h"
#include "../table_widget/input.h"
#include "../content/folder_table.h"
#include <boost/smart_ptr/local_shared_ptr.hpp>
#include <spdlog/fmt/fmt.h>

using namespace syncspirit;
using namespace model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

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
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using table_t = content::folder_table_t;
        using devices_ptr_t = table_t::shared_devices_t;

        auto f = folder_info.get_folder();
        auto prev = content->get_widget();
        auto shared_with = devices_ptr_t(new model::devices_map_t{});
        auto non_shared_with = devices_ptr_t(new model::devices_map_t{});

        auto cluster = supervisor.get_cluster();
        for (auto it : cluster->get_devices()) {
            auto &device = it.item;
            if (device != cluster->get_device()) {
                if (f->is_shared_with(*device)) {
                    shared_with->put(device);
                } else {
                    non_shared_with->put(device);
                }
            }
        }

        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        auto folder_descr = table_t::folder_description_t{*folder_info.get_folder(),
                                                          folder_info.get_file_infos().size(),
                                                          folder_info.get_index(),
                                                          folder_info.get_max_sequence(),
                                                          shared_with,
                                                          non_shared_with};

        return new table_t(*this, folder_descr, table_t::mode_t::edit, x, y, w, h);
    });
    return true;
}
