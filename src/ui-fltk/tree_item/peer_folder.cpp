#include "peer_folder.h"
#include <algorithm>
#include <boost/filesystem.hpp>

using namespace syncspirit::fltk::tree_item;
namespace bfs = boost::filesystem;

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
    if (expandend) {
        return;
    }

    using files_t = std::vector<model::file_info_ptr_t>;
    assert(children());
    auto dummy = child(0);
    Fl_Tree_Item::remove_child(dummy);
    expandend = true;

    auto &files_map = folder_info.get_file_infos();
    auto files = files_t();
    files.reserve(files_map.size());
    for (auto &it : folder_info.get_file_infos()) {
        files.push_back(it.item);
    }
    auto sorter = [](const model::file_info_ptr_t &l, const model::file_info_ptr_t &r) {
        return l->get_name() < r->get_name();
    };
    std::sort(files.begin(), files.end(), sorter);

    for (auto &file : files) {
        auto path = bfs::path(file->get_name());
        auto dir = locate_dir(path.parent_path());
        dir->add_file(*file);
    }
    tree()->redraw();

    supervisor.get_logger()->info("expanded, {} files", files.size());
}
