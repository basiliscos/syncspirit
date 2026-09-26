// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "messages.h"
#include <cassert>

namespace syncspirit::model::payload {

model_interrupt_t::model_interrupt_t(r::message_base_t *source_) noexcept : source{source_} {}

model_interrupt_t::model_interrupt_t(apply_context_t &&other) noexcept {
    original = other.source ? other.source : other.original;
    total_blocks = other.total_blocks;
    total_files = other.total_files;
    loaded_blocks = other.loaded_blocks;
    loaded_files = other.loaded_files;
    diff = other.diff;
    assert(original);
    assert(diff);
}

model_interrupt_t::model_interrupt_t(const model_interrupt_t &other, diff::cluster_diff_t *diff_) noexcept {
    source = other.source ? other.source : other.original.get();
    total_blocks = other.total_blocks;
    total_files = other.total_files;
    loaded_blocks = other.loaded_blocks;
    loaded_files = other.loaded_files;
    diff = diff_;
}

apply_context_t::apply_context_t(r::message_base_t *source, const void *message_payload_) noexcept : parent_t(source) {
    message_payload = message_payload_;
}

apply_context_t::apply_context_t(model_interrupt_t &msg) noexcept : parent_t(msg, nullptr) {}

} // namespace syncspirit::model::payload
