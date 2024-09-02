#include "peer_folder.h"

using namespace syncspirit::fltk::tree_item;

peer_folder_t::peer_folder_t(model::folder_info_t &folder_info_, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, true), folder_info{folder_info_}, expandend{false} {
    update_label();
    folder_info.set_augmentation(get_proxy());

    if (folder_info.get_file_infos().size()) {
        add(prefs(), "[dummy]", new Fl_Tree_Item(tree));
        tree->close(this, 0);
    }
}

void peer_folder_t::update_label() {
    auto &folder = *folder_info.get_folder();
    auto value = fmt::format("{}, {}", folder.get_label(), folder.get_id());
    label(value.data());
    tree()->redraw();
}

void peer_folder_t::on_open() {
    assert(children());
    auto dummy = child(0);
    Fl_Tree_Item::remove_child(dummy);
    expandend = true;
    supervisor.get_logger()->info("expanded");
}
