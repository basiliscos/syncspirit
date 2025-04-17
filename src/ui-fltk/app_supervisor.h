// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024-2025 Ivan Baidakou

#pragma once

#include "content.h"
#include "config/main.h"
#include "utils/log.h"
#include "net/messages.h"
#include "model/messages.h"
#include "model/diff/apply_controller.h"
#include "model/diff/cluster_visitor.h"
#include "model/diff/load/load_cluster.h"
#include "model/misc/sequencer.h"

#include <spdlog/sinks/dist_sink.h>
#include <rotor/fltk.hpp>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Group.H>
#include <filesystem>
#include <chrono>
#include <set>

namespace syncspirit::fltk {

namespace r = rotor;
namespace rf = r::fltk;
namespace bfs = std::filesystem;
namespace sys = boost::system;
namespace outcome = boost::outcome_v2;

struct app_supervisor_t;
struct main_window_t;
struct tree_item_t;
struct augmentation_entry_base_t;

struct db_info_viewer_t {
    virtual void view(const net::payload::db_info_response_t &) = 0;
};

struct db_info_viewer_guard_t {
    db_info_viewer_guard_t(main_window_t *main_window);
    db_info_viewer_guard_t(const db_info_viewer_guard_t &) = delete;
    db_info_viewer_guard_t(db_info_viewer_guard_t &&);

    db_info_viewer_guard_t &operator=(db_info_viewer_guard_t &&);
    ~db_info_viewer_guard_t();
    void reset();

  private:
    main_window_t *main_window;
};

using dist_sink_t = std::shared_ptr<spdlog::sinks::dist_sink_mt>;

struct app_supervisor_config_t : rf::supervisor_config_fltk_t {
    using parent_t = rf::supervisor_config_fltk_t;
    using parent_t::parent_t;

    dist_sink_t dist_sink;
    bfs::path config_path;
    config::main_t app_config;
};

template <typename Actor> struct app_supervisor_config_builder_t : rf::supervisor_config_fltk_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = rf::supervisor_config_fltk_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&dist_sink(dist_sink_t value) && noexcept {
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

struct callback_t : model::arc_base_t<callback_t> {
    virtual ~callback_t() = default;
    virtual void eval() = 0;
};
using callback_ptr_t = model::intrusive_ptr_t<callback_t>;

struct app_supervisor_t : rf::supervisor_fltk_t,
                          private model::diff::cluster_visitor_t,
                          private model::diff::apply_controller_t {
    using parent_t = rf::supervisor_fltk_t;
    using config_t = app_supervisor_config_t;
    template <typename Actor> using config_builder_t = app_supervisor_config_builder_t<Actor>;

    explicit app_supervisor_t(config_t &config);
    app_supervisor_t(const app_supervisor_t &) = delete;
    ~app_supervisor_t();

    void configure(r::plugin::plugin_base_t &plugin) noexcept override;
    void shutdown_finish() noexcept override;

    dist_sink_t &get_dist_sink();
    const bfs::path &get_config_path();
    config::main_t &get_app_config();
    model::cluster_t *get_cluster();
    model::sequencer_t &get_sequencer();
    void write_config(const config::main_t &) noexcept;

    std::string get_uptime() noexcept;
    utils::logger_t &get_logger() noexcept;
    void add_sink(spdlog::sink_ptr ui_sink);

    template <typename Fn> auto replace_content(Fn constructor) noexcept -> content_t * {
        if (!content) {
            content = constructor(content);
        } else {
            auto parent = content->get_widget()->parent();
            auto prev = content;
            parent->remove(prev->get_widget());
            parent->begin();
            content = constructor(prev);
            log->trace("replacing content {} -> {}", (void *)prev, (void *)content);
            content->refresh();
            parent->add(content->get_widget());
            parent->end();
            delete prev;
            // parent->resizable(content);
            parent->redraw();
        }
        return content;
    }

    template <typename Payload, typename... Args> void send_model(Args &&...args) {
        send<Payload>(coordinator, std::forward<Args>(args)...);
    }

    template <typename Payload, typename... Args> void send_sink(Args &&...args) {
        auto message = r::make_routed_message<Payload>(coordinator, sink, std::forward<Args>(args)...);
        put(std::move(message));
    }

    template <typename Fn> void with_cluster(Fn &&fn) {
        if (cluster) {
            fn();
        }
    }

    void set_main_window(main_window_t *window);
    main_window_t *get_main_window();
    void set_devices(tree_item_t *node);
    void set_folders(tree_item_t *node);
    void set_pending_devices(tree_item_t *node);
    void set_ignored_devices(tree_item_t *node);
    void set_show_deleted(bool value);
    void set_show_colorized(bool value);
    void postpone_update(augmentation_entry_base_t &);

    callback_ptr_t call_select_folder(std::string_view folder_id);
    callback_ptr_t call_share_folders(std::string_view folder_id, std::vector<utils::bytes_t> devices);
    db_info_viewer_guard_t request_db_info(db_info_viewer_t *viewer);
    void request_load_model();
    r::address_ptr_t &get_coordinator_address();

    std::uint32_t mask_nodes() const noexcept;

  private:
    using clock_t = std::chrono::high_resolution_clock;
    using time_point_t = typename clock_t::time_point;
    using callbacks_t = std::list<callback_ptr_t>;

    void on_model_response(model::message::model_response_t &res) noexcept;
    void on_model_update(model::message::model_update_t &message) noexcept;
    void on_db_info_response(net::message::db_info_response_t &res) noexcept;
    void redisplay_folder_nodes(bool refresh_labels);

    outcome::result<void> operator()(const model::diff::advance::advance_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::load::load_cluster_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::local::io_failure_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::local::scan_start_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_pending_folders_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_pending_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::add_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &, void *) noexcept override;
    outcome::result<void> operator()(const model::diff::peer::update_folder_t &, void *) noexcept override;

    outcome::result<void> apply(const model::diff::load::blocks_t &, model::cluster_t &cluster) noexcept override;
    outcome::result<void> apply(const model::diff::load::file_infos_t &, model::cluster_t &cluster) noexcept override;
    outcome::result<void> apply(const model::diff::load::load_cluster_t &, model::cluster_t &cluster) noexcept override;

    model::sequencer_ptr_t sequencer;
    time_point_t started_at;
    r::address_ptr_t coordinator;
    r::address_ptr_t sink;
    utils::logger_t log;
    dist_sink_t dist_sink;
    spdlog::sink_ptr ui_sink;
    bfs::path config_path;
    config::main_t app_config;
    config::main_t app_config_original;
    model::cluster_ptr_t cluster;
    content_t *content;
    tree_item_t *devices;
    tree_item_t *folders;
    tree_item_t *pending_devices;
    tree_item_t *ignored_devices;
    db_info_viewer_t *db_info_viewer;
    callbacks_t callbacks;
    main_window_t *main_window;
    std::size_t loaded_blocks;
    std::size_t loaded_files;
#if 0
    updated_entries_t *updated_entries;
#endif
    const model::diff::load::load_cluster_t *load_cluster;

    friend struct db_info_viewer_guard_t;
};

} // namespace syncspirit::fltk
