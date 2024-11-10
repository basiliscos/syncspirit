#include "peer_entry.h"
#include "../content/remote_file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

peer_entry_t::peer_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry_)
    : parent_t(supervisor, tree, true), entry{entry_} {
    entry.set_augmentation(get_proxy());
}

auto peer_entry_t::get_entry() -> model::file_info_t * { return &entry; }

void peer_entry_t::update_label() {
    auto &entry = *get_entry();
    auto name = get_entry()->get_path().filename().string();
    label(name.c_str());
    if (entry.is_deleted()) {
        labelfgcolor(FL_DARK1);
    } else if (entry.is_global()) {
        labelfgcolor(FL_GREEN);
    } else {
        labelfgcolor(FL_BLACK);
    }
    tree()->redraw();
}

bool peer_entry_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new content::remote_file_table_t(*this, x, y, w, h);
    });
    return true;
}

void peer_entry_t::on_update() {
    parent_t::on_update();
    auto &entry = *get_entry();
    if (entry.is_deleted()) {
        auto host = static_cast<entry_t *>(parent());
        bool show_deleted = supervisor.get_app_config().fltk_config.display_deleted;
        if (!show_deleted) {
            host->remove_child(this);
        } else {
            host->deleted_items.emplace(this);
        }
    }
}

auto peer_entry_t::make_entry(model::file_info_t &file) -> entry_t * {
    return new peer_entry_t(supervisor, tree(), file);
}
