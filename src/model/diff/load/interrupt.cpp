// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "interrupt.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"

using namespace syncspirit::model::diff::load;

auto interrupt_t::apply_forward(apply_controller_t &controller, void *custom) const noexcept -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto interrupt_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    return visitor(*this, custom);
}
