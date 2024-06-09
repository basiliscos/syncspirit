#include "generic_remove.h"

using namespace syncspirit::model::diff::modify;

generic_remove_t::generic_remove_t(keys_t keys_) noexcept : keys{std::move(keys_)} {}
