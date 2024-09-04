#include "peer_dir_base.h"

using namespace syncspirit::fltk::tree_item;

peer_dir_base_t::peer_dir_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation)
    : tree_item_t(supervisor, tree, has_augmentation), direct_dirs_count{0} {}

auto peer_dir_base_t::locate_dir(const bfs::path &parent) -> virtual_dir_t * {
    if (parent.empty()) {
        return this;
    }
    assert(0 && "TODO");
}

void peer_dir_base_t::add_file(model::file_info_t &file) {
    auto label = file.get_path().filename();
    within_tree([&]() {
        auto node = new tree_item_t(supervisor, tree(), false);
        node->label(label.c_str());
        add(prefs(), label.c_str(), node);
        return node;
    });
}
