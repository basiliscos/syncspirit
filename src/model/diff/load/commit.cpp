// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "commit.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

commit_t::commit_t(rotor::message_ptr_t commit_message_) noexcept : commit_message{std::move(commit_message_)} {}

auto commit_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto commit_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
