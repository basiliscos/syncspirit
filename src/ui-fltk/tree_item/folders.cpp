#include "folders.h"

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

augmentation_ptr_t folders_t::add_folder(model::folder_info_t &folder_info) {
    auto augmentation = within_tree([&]() {
        auto item = new tree_item_t(supervisor, tree(), true);
        auto folder = folder_info.get_folder();
        auto label = fmt::format("{}, {}", folder->get_id(), folder->get_label());
        item->label(label.data());
        return insert_by_label(item)->get_proxy();
    });
    update_label();
    return augmentation;
}
