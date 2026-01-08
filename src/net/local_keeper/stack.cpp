// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "stack.h"

using namespace syncspirit::net::local_keeper;

dirs_stack_t::dirs_stack_t(stack_t &outer_) : outer{outer_} {}

dirs_stack_t::~dirs_stack_t() {
    while (!empty()) {
        auto item = std::move(front());
        outer.push_front(std::move(item));
        pop_front();
    }
}
