#include "peer_entry.h"
#include "../content/remote_file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

peer_entry_t::peer_entry_t(app_supervisor_t &supervisor, Fl_Tree *tree, model::file_info_t &entry_)
    : parent_t(supervisor, tree, true), entry{entry_} {
    entry.set_augmentation(get_proxy());
}

auto peer_entry_t::get_entry() -> model::file_info_t * { return &entry; }

bool peer_entry_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new content::remote_file_table_t(*this, x, y, w, h);
    });
    return true;
}
