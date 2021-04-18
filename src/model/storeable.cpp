#include "storeable.h"

using namespace syncspirit::model;

bool storeable_t::is_dirty() const noexcept { return dirty; }

void storeable_t::mark_dirty() noexcept { dirty = true; }

void storeable_t::unmark_dirty() noexcept { dirty = false; }
