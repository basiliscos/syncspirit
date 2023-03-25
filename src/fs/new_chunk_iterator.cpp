
#include "new_chunk_iterator.h"

using namespace syncspirit::fs;

bool new_chunk_iterator_t::is_complete() const noexcept { return out_of_order.empty() && unfinished.empty(); }
