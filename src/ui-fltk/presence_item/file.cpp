// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "file.h"
#include "content/file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

using F = syncspirit::presentation::presence_t::features_t;

bool file_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new content::file_table_t(*this, x, y, w, h);
    });
    return true;
}
