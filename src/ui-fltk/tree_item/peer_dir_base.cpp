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
    bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
    auto name = file.get_path().filename().string();
    auto name_provider = [this](int index) { return std::string_view(child(index)->label()); };
    auto start_index = int{0};
    auto end_index = int{0};
    auto t = tree();
    auto node = within_tree([&]() -> tree_item_t * {
        if (file.is_dir()) {
            end_index = static_cast<int>(dirs_map.size()) - 1;
            auto dir = new peer_dir_t(supervisor, t, true);
            dirs_map[name] = dir;
            return dir;
        } else {
            start_index = dirs_map.size();
            end_index = children() - 1;
            return new tree_item_t(supervisor, tree(), true);
        }
    });

    node->label(name.c_str());
    file.set_augmentation(node->get_proxy());
    if (deleted) {
        node->labelfgcolor(FL_DARK1);
    }
    if (!deleted || show_deleted) {
        auto pos = bisect(name, start_index, end_index, children(), name_provider);
        auto tmp_node = insert(prefs(), "", pos);
        replace_child(tmp_node, node);
        if (file.is_dir()) {
            t->close(node, 0);
        }
    } else {
        orphaned_items.emplace_back(node);
    }
}
