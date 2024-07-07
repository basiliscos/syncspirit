#pragma once

#include "config/main.h"
#include "utils/log.h"
#include "model/messages.h"
#include "model/diff/block_visitor.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/load/load_cluster.h"

#include <set>
#include <chrono>
#include <rotor/fltk.hpp>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Group.H>
#include <boost/filesystem.hpp>

namespace syncspirit::fltk {

namespace r = rotor;
namespace rf = r::fltk;
namespace bfs = boost::filesystem;
namespace outcome = boost::outcome_v2;

struct app_supervisor_t;
struct tree_item_t;

struct app_supervisor_config_t : rf::supervisor_config_fltk_t {
    using parent_t = rf::supervisor_config_fltk_t;
    using parent_t::parent_t;

    utils::dist_sink_t dist_sink;
    bfs::path config_path;
    config::main_t app_config;
};

template <typename Actor> struct app_supervisor_config_builder_t : rf::supervisor_config_fltk_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = rf::supervisor_config_fltk_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&dist_sink(utils::dist_sink_t &value) && noexcept {
        parent_t::config.dist_sink = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&config_path(const bfs::path &value) && noexcept {
        parent_t::config.config_path = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }

    builder_t &&app_config(const config::main_t &value) && noexcept {
        parent_t::config.app_config = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct app_supervisor_t : rf::supervisor_fltk_t,
                          private model::diff::cluster_visitor_t,
                          private model::diff::contact_visitor_t,
                          private model::diff::block_visitor_t {
    using parent_t = rf::supervisor_fltk_t;
    using config_t = app_supervisor_config_t;
    template <typename Actor> using config_builder_t = app_supervisor_config_builder_t<Actor>;

    explicit app_supervisor_t(config_t &config);
    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    utils::dist_sink_t &get_dist_sink();
    const bfs::path &get_config_path();
    const config::main_t &get_app_config();
    model::cluster_ptr_t &get_cluster();

    std::string get_uptime() noexcept;
    utils::logger_t &get_logger() noexcept;

    template <typename Fn> void replace_content(Fn constructor) noexcept {
        if (!content) {
            content = constructor(content);
        } else {
            auto parent = content->parent();
            auto prev = content;
            parent->remove(prev);
            parent->begin();
            content = constructor(prev);
            parent->add(content);
            parent->end();
            delete prev;
            // parent->resizable(content);
            parent->redraw();
        }
    }

    template <typename Payload, typename... Args> void send_model(Args &&...args) {
        send<Payload>(coordinator, std::forward<Args>(args)...);
    }

    void set_devices(tree_item_t *devices);
    void set_unknown_devices(tree_item_t *devices);

  private:
    using clock_t = std::chrono::high_resolution_clock;
    using time_point_t = typename clock_t::time_point;
    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_contact_update(model::message::contact_update_t &message) noexcept;
    void on_block_update(model::message::block_update_t &message) noexcept;

    outcome::result<void> operator()(const model::diff::load::load_cluster_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_unknown_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_unknown_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::cluster_update_t &, void *) noexcept override;

    time_point_t started_at;
    r::address_ptr_t coordinator;
    utils::logger_t log;
    utils::dist_sink_t dist_sink;
    bfs::path config_path;
    config::main_t app_config;
    model::cluster_ptr_t cluster;
    Fl_Widget *content;
    tree_item_t *devices;
    tree_item_t *unkwnown_devices;
};

} // namespace syncspirit::fltk
