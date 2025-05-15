// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "missing_item_presence.h"
#include "presentation/missing_file_presence.h"
#include "../content/remote_file_table.h"

using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

missing_item_presence_t::missing_item_presence_t(presence_item_t *host_, presentation::presence_t &presence_)
    : parent_t(presence_, host_->supervisor, host_->tree(), false), host{host_} {
    using F = presentation::presence_t::features_t;
    assert(presence_.get_features() & F::missing);
    update_label();
    if (presence_.get_entity()->get_children().size()) {
        populate_dummy_child();
    }
    auto &p = static_cast<presentation::missing_file_presence_t &>(presence_);
    p.add(this);
}

missing_item_presence_t::~missing_item_presence_t() {
    auto p = static_cast<presentation::missing_file_presence_t *>(presence);
    presence = {};
    p->remove(this);
}

auto missing_item_presence_t::get_device() const -> const model::device_t * {
    return host->get_presence().get_device();
}

void missing_item_presence_t::on_delete() noexcept {
    select_other();
    if (auto p = parent(); p) {
        if (auto index = p->find_child(this); index >= 0) {
            auto holder = dynamic_cast<presence_item_t *>(p)->safe_detach(index);
            if (!presence) { // it is already in d-tor
                holder.detach();
            }
        }
    }
}

bool missing_item_presence_t::on_select() {
    content = supervisor.replace_content([&](content_t *content) -> content_t * {
        auto prev = content->get_widget();
        int x = prev->x(), y = prev->y(), w = prev->w(), h = prev->h();
        return new content::remote_file_table_t(*this, x, y, w, h);
    });
    return true;
}
