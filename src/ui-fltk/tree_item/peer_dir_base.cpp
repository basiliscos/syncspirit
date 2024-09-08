#include "peer_dir_base.h"
#include "peer_dir.h"

using namespace syncspirit::fltk::tree_item;

peer_dir_base_t::peer_dir_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation)
    : tree_item_t(supervisor, tree, has_augmentation), direct_dirs_count{0} {}

auto peer_dir_base_t::locate_dir(const bfs::path &parent) -> virtual_dir_t * {
    auto current = (virtual_dir_t *)(this);
    for (auto &piece : parent) {
        auto name = piece.string();
        current = current->locate_own_dir(name);
    }
    return current;
}

virtual_dir_t *peer_dir_base_t::locate_own_dir(std::string_view name) {
    auto pos = bisect_pos(name, 0, direct_dirs_count - 1);
    if (direct_dirs_count && pos < direct_dirs_count) {
        auto untyped_node = child(pos);
        auto node = dynamic_cast<virtual_dir_t *>(untyped_node);
        if (untyped_node->label() == name) {
            return static_cast<virtual_dir_t *>(node);
        }
    }
    return within_tree([&]() {
        auto t = tree();
        auto node = new peer_dir_t(supervisor, t, false);
        node->label(name.data());
        auto tmp_node = insert(prefs(), "", pos);
        replace_child(tmp_node, node);
        t->close(node, 0);
        ++direct_dirs_count;
        return node;
    });
}

void peer_dir_base_t::add_entry(model::file_info_t &file) {
    auto label = file.get_path().filename().string();
    if (file.is_dir()) {
        locate_own_dir(label);
        return;
    }
    within_tree([&]() {
        auto node = new tree_item_t(supervisor, tree(), false);
        node->label(label.c_str());
        if (file.is_deleted()) {
            node->labelfgcolor(FL_DARK1);
        }
        insert_by_label(node, direct_dirs_count);
        return node;
    });
}
