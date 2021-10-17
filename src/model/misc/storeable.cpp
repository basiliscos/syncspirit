#include "storeable.h"

using namespace syncspirit::model;

bool storeable_t::is_dirty() const noexcept { return flags & F_DIRTY; }

void storeable_t::mark_dirty() noexcept { flags |= F_DIRTY; }

void storeable_t::unmark_dirty() noexcept { flags &= ~F_DIRTY; }

void storeable_t::mark_deleted() noexcept { flags |= F_DELETED; }

bool storeable_t::is_deleted() const noexcept { return flags & F_DELETED; }
