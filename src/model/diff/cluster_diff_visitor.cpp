#include "cluster_diff_visitor.h"

using namespace syncspirit::model::diff;

auto cluster_diff_visitor_t::operator ()(const cluster::unknown_folders_t &) noexcept -> outcome::result<void>  {
    return outcome::success();
}
