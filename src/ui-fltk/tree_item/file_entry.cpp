#include "file_entry.h"

#if 0
#include "../content/remote_file_table.h"
#endif

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

#if 0
file_entry_t::file_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t *entry_,
                           std::string filename_)
    : parent_t(supervisor, tree, true), entry{entry_}, filename{std::move(filename_)} {
    if (entry) {
        entry->set_augmentation(get_proxy());
    }
}

void file_entry_t::update_label() {
    if (entry) {
        filename = get_entry()->get_path().filename().string();
        auto context = color_context_t::unknown;
        if (entry->is_deleted()) {
            context = color_context_t::deleted;
        } else if (entry->is_link()) {
            context = color_context_t::link;
        } else if (entry->is_global()) {
            context = color_context_t::actualized;
        }
        auto color = supervisor.get_color(context);
        labelfgcolor(color);
    }
    label(filename.c_str());
    tree()->redraw();
}

bool file_entry_t::on_select() {
    if (entry) {
        content = supervisor.replace_content([&](content_t *content) -> content_t * {
            auto prev = content->get_widget();
            int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
            return new content::remote_file_table_t(*this, x, y, w, h);
        });
    }
    return true;
}

void file_entry_t::on_update() {
    parent_t::on_update();
    if (entry) {
        auto host = static_cast<entry_t *>(parent());
        bool to_be_displayed = supervisor.get_app_config().fltk_config.display_deleted;
        if (host) {
            if (entry->is_deleted()) {
                if (!to_be_displayed) {
                    host->remove_child(this);
                } else {
                    host->deleted_items.emplace(this);
                }
            }
        } else if (!host && !entry->is_deleted() || to_be_displayed) {
            auto folder_info = entry->get_folder_info();
            auto augmentation = static_cast<augmentation_base_t *>(folder_info->get_augmentation().get());
            assert(augmentation);
            auto owner = augmentation->get_owner();
            auto folder_entry = dynamic_cast<tree_item::entry_t *>(owner);
            auto path = bfs::path(entry->get_name());
            auto parent_entry = folder_entry->locate_dir(path.parent_path());
            parent_entry->remove_node(this);
            parent_entry->insert_node(this);
        }
    }
}

auto file_entry_t::get_entry() -> model::file_info_t * { return entry; }

auto file_entry_t::make_entry(model::file_info_t *file, std::string filename) -> entry_t * {
    return new file_entry_t(supervisor, tree(), file, std::move(filename));
}

void file_entry_t::assign(entry_t &new_entry) {
    auto &e = dynamic_cast<file_entry_t &>(new_entry);
    entry = e.entry;
    entry->set_augmentation(get_proxy());
    e.get_proxy()->release_onwer();
    on_update();
}
#endif
