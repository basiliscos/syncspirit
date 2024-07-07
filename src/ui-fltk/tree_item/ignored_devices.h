#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct ignored_devices_t : tree_item_t {
    using parent_t = tree_item_t;
    ignored_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void build_tree();
    void update_label();
#if 0
    void operator()(model::message::model_update_t &) override;
    void operator()(model::message::contact_update_t &) override;
    outcome::result<void> operator()(const diff::load::load_cluster_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::load::ignored_devices_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::add_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::remove_ignored_device_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::modify::update_peer_t &, void *) noexcept override;
    outcome::result<void> operator()(const diff::contact::ignored_connected_t &, void *) noexcept override;
    model_subscription_t model_sub;
#endif
    void add_device(const model::ignored_device_ptr_t &device);
};

} // namespace syncspirit::fltk::tree_item
