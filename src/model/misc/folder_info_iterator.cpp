// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "folder_info_iterator.h"
#include "model/folder_info.h"
#include <algorithm>
#include <cassert>

using namespace syncspirit::model;

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

folder_info_iterator_t::folder_info_iterator_t(folder_info_ptr_t fi_) noexcept:
    fi{std::move(fi_)} {
    reset(true);
}

bool folder_info_iterator_t::is_finished() const noexcept {
    return current == nullptr;
}

void folder_info_iterator_t::reset(bool full) noexcept {
    current = fi.get();
    if (full) {
        seen_sequence = 0;
        min_sequence = 0;
        max_sequence = fi->get_max_sequence();
    }
}

void folder_info_iterator_t::set_min_sequence(std::int64_t value) noexcept {
    min_sequence = value;
}

file_info_t* folder_info_iterator_t::next() noexcept {
    if (current) {
        for (auto file = advance(); file; file = advance()) {
            auto seq = file->get_sequence();
            if (seq > max_sequence) {
                continue;
            }
            if (seq < min_sequence) {
                continue;
            }
            if (seen_sequence < seq) {
                seen_sequence = seq;
            }
            if (seq > seen_sequence) {
                seen_sequence = seq;
            }
            return file;
        }
        min_sequence = seen_sequence + 1;
        auto seq = fi->get_max_sequence();
        if (seq != max_sequence) {
            current = fi.get();
            max_sequence = fi->get_max_sequence();
            return next();
        }
    }
    return nullptr;
}

file_info_t* folder_info_iterator_t::advance() noexcept {
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
        return static_cast<model::file_info_t*>(current);
    }
    return nullptr;
}
