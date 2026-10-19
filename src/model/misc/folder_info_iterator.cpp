// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "folder_info_iterator.h"
#include "model/folder_info.h"
#include <algorithm>
#include <cassert>

using namespace syncspirit::model;

folder_info_iterator_t::folder_info_iterator_t(folder_info_ptr_t fi_) noexcept:
    fi{std::move(fi_)} {
    current = fi.get();
}

static presence_t* next_sibling(presence_t* current) noexcept {
    auto parent = current->get_parent();
    if (parent) {
        auto& children = parent->get_children();
        auto b = children.begin();
        auto e = children.end();
        auto it = std::lower_bound(b, e, current, presence_t::child_comparator_t{});
        assert(it != e);
        ++it;
        if (it != e) {
            return *it;
        }
    }
    return nullptr;
}

file_info_t* folder_info_iterator_t::next() noexcept {
    if (current) {
        auto children = &current->get_children();
        if (children->size() > 0) {
            current = children->front();
        } else if (current->parent) {
            REDO:
            if (auto sibling = next_sibling(current); sibling) {
                current = sibling;
            } else {
                current = current->parent;
                if (current) {
                    goto REDO;
                }
            }
        }  else {
            current = nullptr;
        }
    }
    if (current) {
        auto r = static_cast<model::file_info_t*>(current);
        auto seq = r->get_sequence();
        if (seq > seen_sequence) {
            seen_sequence = seq;
        }
        return r;
    }
    return nullptr;
}
