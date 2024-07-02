#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct unknown_devices_t : tree_item_t,
                           private model_listener_t,
                           private model::diff::cluster_visitor_t,
                           private model::diff::contact_visitor_t {
    using parent_t = tree_item_t;
    unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void build_tree();
    void update_label();
    void operator()(model::message::model_update_t &) override;
    void operator()(model::message::contact_update_t &) override;
    outcome::result<void> operator()(const diff::load::load_cluster_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::load::unknown_devices_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::add_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::remove_unknown_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::contact::unknown_connected_t &, void *) noexcept override;
    void add_device(const model::unknown_device_ptr_t &device);

    model_subscription_t model_sub;
};

} // namespace syncspirit::fltk::tree_item
