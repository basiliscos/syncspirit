#include "peer_dir_base.h"
#include "peer_dir.h"
#include "../utils.hpp"

using namespace syncspirit::fltk::tree_item;

peer_dir_base_t::peer_dir_base_t(app_supervisor_t &supervisor, Fl_Tree *tree, bool has_augmentation)
    : tree_item_t(supervisor, tree, has_augmentation) {}

peer_dir_base_t::~peer_dir_base_t() {
    for (auto item : orphaned_items) {
        delete item;
    }
    orphaned_items.clear();
}

auto peer_dir_base_t::locate_dir(const bfs::path &parent) -> virtual_dir_t * {
    auto current = (virtual_dir_t *)(this);
    for (auto &piece : parent) {
        auto name = piece.string();
        current = current->locate_own_dir(name);
    }
    return current;
}

virtual_dir_t *peer_dir_base_t::locate_own_dir(std::string_view name) {
    if (name.empty()) {
        return this;
    }

    auto it = dirs_map.find(name);
    assert(it != dirs_map.end());
    return dynamic_cast<virtual_dir_t *>(it->second);
}

void peer_dir_base_t::add_entry(model::file_info_t &file) {
    bool deleted = file.is_deleted();
    auto name = file.get_path().filename().string();
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    if (file.is_dir()) {
        within_tree([&]() {
            auto pos = bisect(name, 0, static_cast<int>(dirs_map.size()) - 1, children(), name_provider);
            auto t = tree();
            auto node = new peer_dir_t(supervisor, t, true);
            node->label(name.data());
            auto tmp_node = insert(prefs(), "", pos);
            replace_child(tmp_node, node);
            t->close(node, 0);
            file.set_augmentation(node->get_proxy());
            dirs_map[name] = node;
            return node;
        });
        return;
    }
    within_tree([&]() {
        auto node = new tree_item_t(supervisor, tree(), false);
        node->label(name.c_str());
        if (deleted) {
            node->labelfgcolor(FL_DARK1);
        }
        insert_by_label(node, static_cast<int>(dirs_map.size()));
        return node;
    });
}
