#include "unknown_folders.h"
#include "../cluster_diff_visitor.h"

using namespace syncspirit::model::diff::cluster;

void unknown_folders_t::apply(cluster_t &cluster) const noexcept {}

void unknown_folders_t::visit(cluster_diff_visitor_t &vistor) const noexcept { vistor(*this); }
