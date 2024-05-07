#pragma once

#include "utils/log.h"
#include "model/messages.h"
#include "model/diff/block_visitor.h"
#include "model/diff/cluster_visitor.h"

#include <rotor/fltk.hpp>
#include <set>

namespace syncspirit::fltk {

struct model_load_listener_t {
    virtual ~model_load_listener_t() = default;
    virtual void operator()(model::message::model_response_t&) = 0;
};

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
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    utils::dist_sink_t& get_dist_sink();
    model::cluster_ptr_t& get_cluster();

    void add(model_load_listener_t* listener) noexcept;
    void remove(model_load_listener_t* listener) noexcept;

private:
    using load_listeners_t = std::set<model_load_listener_t*>;
    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_block_update(model::message::block_update_t &message) noexcept;

    r::address_ptr_t coordinator;
    utils::logger_t log;
    utils::dist_sink_t dist_sink;
    model::cluster_ptr_t cluster;
    load_listeners_t load_listeners;
};

} // namespace syncspirit::fltk
