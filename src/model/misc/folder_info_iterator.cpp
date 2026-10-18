// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "folder_info_iterator.h"
#include "model/folder_info.h"

using namespace syncspirit::model;

folder_info_iterator_t::folder_info_iterator_t(folder_info_ptr_t fi_) noexcept:
    fi{std::move(fi_)} {
    current = fi.get();
}

file_info_t* folder_info_iterator_t::next() noexcept {
    auto r = (model::file_info_t*){nullptr};
    if (current->next) {
        current->normalize();
        current = current->next;
        r = static_cast<model::file_info_t*>(current);
        auto seq = r->get_sequence();
        if (seen_sequence < seq) {
            seen_sequence = seq;
        }
    }
    return r;
}
