// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#pragma once
#include "model/diff/cluster_diff.h"
#include "model/diff/apply_controller.h"
#include <rotor/message.h>

namespace syncspirit::model::diff::load {

struct SYNCSPIRIT_API commit_t final : cluster_diff_t {
    using parent_t = cluster_diff_t;

    commit_t(rotor::message_ptr_t commit_message) noexcept;

    outcome::result<void> apply_forward(cluster_t &, apply_controller_t &, void *) const noexcept override;
    rotor::message_ptr_t commit_message;
};

} // namespace syncspirit::model::diff::load
