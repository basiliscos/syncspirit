#include "peer_folder.h"
#include "peer_entry.h"
#include "../static_table.h"
#include <boost/filesystem.hpp>

using namespace syncspirit;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;
namespace bfs = boost::filesystem;

namespace {

struct my_table_t;

struct my_table_t : static_table_t {
    using parent_t = static_table_t;

    my_table_t(peer_folder_t &container_, int x, int y, int w, int h) : parent_t(x, y, w, h), container{container_} {
        auto augmentation = static_cast<augmentation_entry_base_t *>(container.get_proxy().get());
        auto fi = augmentation->get_folder();
        auto &folder = *fi->get_folder();
        auto data = table_rows_t();

        index_cell = new static_string_provider_t();
        max_sequence_cell = new static_string_provider_t();
        entries_cell = new static_string_provider_t();
        entries_size_cell = new static_string_provider_t();

        data.push_back({"label", new static_string_provider_t(folder.get_label())});
        data.push_back({"id", new static_string_provider_t(folder.get_id())});
        data.push_back({"index", index_cell});
        data.push_back({"max sequence", max_sequence_cell});
        data.push_back({"entries", entries_cell});
        data.push_back({"entries size", entries_size_cell});
        assign_rows(std::move(data));
        refresh();
    }

    void refresh() override {
        auto augmentation = static_cast<augmentation_entry_base_t *>(container.get_proxy().get());
        auto fi = augmentation->get_folder();
        std::size_t entries_size = 0;

        index_cell->update(fmt::format("0x{:x}", fi->get_index()));
        max_sequence_cell->update(fmt::format("{}", fi->get_max_sequence()));
        entries_cell->update(fmt::format("{}", fi->get_file_infos().size()));
        entries_size_cell->update(fmt::format("{}", entries_size));

        redraw();
    }

    peer_folder_t &container;
    static_string_provider_ptr_t index_cell;
    static_string_provider_ptr_t max_sequence_cell;
    static_string_provider_ptr_t entries_cell;
    static_string_provider_ptr_t entries_size_cell;
};
} // namespace

peer_folder_t::peer_folder_t(model::folder_info_t &folder_info, app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree, {}) {
    augmentation = new augmentation_entry_root_t(folder_info, this);
    folder_info.set_augmentation(get_proxy());
    update_label();

    add(prefs(), "[dummy]", new tree_item_t(supervisor, tree, false));
    tree->close(this, 0);
}

void peer_folder_t::update_label() {
    auto entry = static_cast<augmentation_entry_root_t *>(augmentation.get());
    auto folder_info = entry->get_folder();
    auto &folder = *folder_info->get_folder();
    auto value = fmt::format("{}, {}", folder.get_label(), folder.get_id());
    label(value.data());
}

bool peer_folder_t::on_select() {
    if (!expanded) {
        on_open();
    }
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        return new my_table_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
    });

    return true;
}
