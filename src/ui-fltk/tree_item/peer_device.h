// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2024 Ivan Baidakou

#pragma once

#include "model/device.h"
#include "../tree_item.h"
#include "../static_table.h"
#include <boost/asio.hpp>
#include <vector>
#include <optional>

namespace syncspirit::fltk::tree_item {

struct peer_device_t : tree_item_t,
                       private model_listener_t,
                       private model::diff::cluster_visitor_t,
                       private model::diff::contact_visitor_t {
    using parent_t = tree_item_t;
    using endpoint_t = std::optional<boost::asio::ip::tcp::endpoint>;

    struct peer_widget_t : widgetable_t {
        using parent_t = widgetable_t;
        peer_widget_t(peer_device_t &container);

        Fl_Widget *get_widget() override;
        virtual void reset();
        virtual bool store(db::Device &device);

        Fl_Widget *widget = nullptr;
        peer_device_t &container;
    };

    using peer_widget_ptr_t = boost::intrusive_ptr<peer_widget_t>;

    using widgets_t = std::vector<peer_widget_ptr_t>;
    peer_device_t(model::device_ptr_t peer, app_supervisor_t &supervisor, Fl_Tree *tree);

    void operator()(model::message::model_update_t &) override;
    void operator()(model::message::contact_update_t &) override;
    outcome::result<void> operator()(const diff::modify::update_peer_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const diff::modify::remove_peer_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const diff::contact::peer_state_t &, void *custom) noexcept override;

    const model::device_t &get_device() const;
    widgetable_ptr_t record(peer_widget_ptr_t);
    std::string get_state();
    void update_label();

    bool on_select() override;
    void on_change();
    void on_remove();
    void on_apply();
    void on_reset();

    model_subscription_t model_sub;
    model::device_ptr_t peer;
    endpoint_t peer_endpoint;
    widgets_t widgets;

    Fl_Widget *apply_button;
    Fl_Widget *reset_button;
};

} // namespace syncspirit::fltk::tree_item
