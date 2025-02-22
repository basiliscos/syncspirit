// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "file_entry.h"

#include "../content/remote_file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

bool file_entry_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new content::remote_file_table_t(*this, x, y, w, h);
    });
    return true;
}
