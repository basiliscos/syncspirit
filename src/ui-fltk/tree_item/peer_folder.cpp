#include "peer_folder.h"

using namespace syncspirit::fltk::tree_item;

peer_folder_t::peer_folder_t(model::folder_info_t &folder_info_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, true), folder_info{folder_info_} {
    update_label();
    folder_info.set_augmentation(get_proxy());
}

void peer_folder_t::update_label() {
    auto &folder = *folder_info.get_folder();
    auto value = fmt::format("{}, {}", folder.get_label(), folder.get_id());
    label(value.data());
    tree()->redraw();
}
