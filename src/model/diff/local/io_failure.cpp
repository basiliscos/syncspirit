// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#include "io_failure.h"
#include "../cluster_visitor.h"
#include "model/diff/apply_controller.h"

using namespace syncspirit::model::diff::local;

io_failure_t::io_failure_t(io_error_t e) noexcept : errors{std::move(e)} {}

io_failure_t::io_failure_t(io_errors_t errors_) noexcept : errors{std::move(errors_)} {}

auto io_failure_t::apply_forward(cluster_t &cluster, apply_controller_t &controller, void *custom) const noexcept
    -> outcome::result<void> {
    return controller.apply(*this, custom);
}

auto io_failure_t::visit(cluster_visitor_t &visitor, void *custom) const noexcept -> outcome::result<void> {
    LOG_TRACE(log, "visiting io_failure_t");
    return visitor(*this, custom);
}
