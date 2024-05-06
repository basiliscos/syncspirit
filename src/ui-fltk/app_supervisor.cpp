#include "app_supervisor.h"

using namespace syncspirit::fltk;

app_supervisor_t::app_supervisor_t(config_t& config):parent_t(config),
    dist_sink(std::move(config.dist_sink)) {
}

auto app_supervisor_t::get_dist_sink() -> utils::dist_sink_t& {
    return dist_sink;
}

auto app_supervisor_t::get_cluster() -> model::cluster_ptr_t& {
    return cluster;
}
