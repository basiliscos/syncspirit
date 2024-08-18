#include "folders.h"
#include "folder.h"
#include "../content/folder_table.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

folders_t::folders_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree, false) {
    supervisor.set_folders(this);
    update_label();
}

void folders_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_folders().size() : 0;
    auto l = fmt::format("folders ({})", count);
    this->label(l.data());
}

augmentation_ptr_t folders_t::add_folder(model::folder_t &folder_info) {
    auto augmentation = within_tree([&]() {
        auto item = new folder_t(folder_info, supervisor, tree());
        return insert_by_label(item)->get_proxy();
    });
    update_label();
    return augmentation;
}

void folders_t::remove_child(tree_item_t *child) {
    parent_t::remove_child(child);
    update_label();
}

void folders_t::select_folder(std::string_view folder_id) {
    auto t = tree();
    for (int i = 0; i < children(); ++i) {
        auto node = static_cast<folder_t *>(child(i));
        if (node->folder.get_id() == folder_id) {
            while (auto selected = t->first_selected_item()) {
                t->deselect(selected);
            }
            t->select(node, 1);
            t->redraw();
            break;
        }
    }
}

bool folders_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        using table_t = content::folder_table_t;
        auto prev = content->get_widget();
        auto folder_data = model::folder_data_t();
        auto shared_with = table_t::shared_devices_t{new model::devices_map_t{}};
        auto non_shared_with = table_t::shared_devices_t{new model::devices_map_t{}};

        auto cluster = supervisor.get_cluster();
        auto &devices = cluster->get_devices();
        auto &self = *cluster->get_device();

        auto index = supervisor.get_sequencer().next_uint64();
        auto description =
            table_t::folder_description_t{std::move(folder_data), 0, index, 0, shared_with, non_shared_with};
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new table_t(*this, description, table_t::mode_t::create, x, y, w, h);
    });
    return true;
}
