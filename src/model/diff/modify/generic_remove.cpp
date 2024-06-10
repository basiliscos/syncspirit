#include "generic_remove.h"

#include <algorithm>

using namespace syncspirit::model::diff::modify;

generic_remove_t::generic_remove_t(keys_t keys_) noexcept : keys{std::move(keys_)} {}

generic_remove_t::generic_remove_t(unique_keys_t keys_) noexcept {
    keys.reserve(keys_.size());
    std::copy(keys_.begin(), keys_.end(), std::back_insert_iterator(keys));
}
