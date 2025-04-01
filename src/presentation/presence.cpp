// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025 Ivan Baidakou

#include "entity.h"
#include "presence.h"

using namespace syncspirit;
using namespace syncspirit::presentation;

presence_t::presence_t(entity_t &entity_, model::device_ptr_t device_) : entity{entity_}, device{std::move(device_)} {}

presence_t::~presence_t() { entity.remove_presense(*this); }
