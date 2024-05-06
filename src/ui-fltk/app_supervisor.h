#pragma once

#include "utils/log.h"
#include "model/messages.h"
#include "model/diff/block_visitor.h"
#include "model/diff/cluster_visitor.h"

#include <rotor/fltk.hpp>

namespace syncspirit::fltk {

namespace r = rotor;
namespace rf = r::fltk;

struct app_supervisor_config_t : rf::supervisor_config_fltk_t {
    using parent_t = rf::supervisor_config_fltk_t;
    using parent_t::parent_t;

    utils::dist_sink_t dist_sink;
};

template <typename Actor> struct app_supervisor_config_builder_t : rf::supervisor_config_fltk_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = rf::supervisor_config_fltk_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&dist_sink(utils::dist_sink_t &value) && noexcept {
        parent_t::config.dist_sink = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct app_supervisor_t: rf::supervisor_fltk_t,
        private model::diff::cluster_visitor_t,
        private model::diff::block_visitor_t {
    using parent_t = rf::supervisor_fltk_t;
    using config_t = app_supervisor_config_t;
    template <typename Actor> using config_builder_t = app_supervisor_config_builder_t<Actor>;

    explicit app_supervisor_t(config_t& config);
    utils::dist_sink_t& get_dist_sink();
    model::cluster_ptr_t& get_cluster();

private:
    utils::dist_sink_t dist_sink;
    model::cluster_ptr_t cluster;
};

} // namespace syncspirit::fltk
