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
        auto &fi = container.folder_info;
        auto &folder = *fi.get_folder();
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
        struct size_visitor_t final : tree_item::file_visitor_t {
            void visit(const model::file_info_t &file, void *data) const override {
                auto sz = reinterpret_cast<size_t *>(data);
                *sz += file.get_size();
            }
        };
        auto &fi = container.folder_info;
        std::size_t entries_size = 0;
        container.apply(size_visitor_t{}, &entries_size);

        index_cell->update(fmt::format("0x{:x}", fi.get_index()));
        max_sequence_cell->update(fmt::format("{}", fi.get_max_sequence()));
        entries_cell->update(fmt::format("{}", fi.get_file_infos().size()));
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
}

void peer_folder_t::on_update() {
    parent_t::on_update();
    if (!expandend && children() == 0 && folder_info.get_file_infos().size()) {
        auto t = tree();
        add(prefs(), "[dummy]", new Fl_Tree_Item(t));
        t->close(this, 0);
    }
}

void peer_folder_t::on_open() {
    if (expandend || !children()) {
        return;
    }

    auto dummy = child(0);
    Fl_Tree_Item::remove_child(dummy);
    expandend = true;

    auto &files_map = folder_info.get_file_infos();
    make_hierarchy(files_map);

    supervisor.get_logger()->debug("{} (peer), expanded {} file records", folder_info.get_folder()->get_label(),
                                   files_map.size());
}

bool peer_folder_t::on_select() {
    if (!expandend) {
        on_open();
    }
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        return new my_table_t(*this, prev->x(), prev->y(), prev->w(), prev->h());
    });

    return true;
}

auto peer_folder_t::get_entry() -> model::file_info_t * { return nullptr; }

auto peer_folder_t::make_entry(model::file_info_t *file, std::string filename) -> entry_t * {
    return new peer_entry_t(supervisor, tree(), file, std::move(filename));
}
