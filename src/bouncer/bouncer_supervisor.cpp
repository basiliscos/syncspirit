// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "bouncer_supervisor.h"
#include "bouncer_actor.h"

using namespace syncspirit::bouncer;

void bouncer_supervisor_t::on_start() noexcept {
    parent_t::on_start();
    create_actor<bouncer_actor_t>().timeout(shutdown_timeout).escalate_failure().finish();
}
