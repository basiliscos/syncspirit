#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct unknown_devices_t : tree_item_t, private model_listener_t, private model::diff::cluster_visitor_t {
    using parent_t = tree_item_t;
    unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void build_tree();
    void update_label();
    void operator()(model::message::model_update_t &) override;
    outcome::result<void> operator()(const diff::load::load_cluster_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const diff::load::unknown_devices_t &, void *custom) noexcept override;
#if 0
    outcome::result<void> operator()(const diff::modify::update_peer_t &, void *custom) noexcept override;
    tree_item_t *get_self_device();
#endif
    void add_device(const model::unknown_device_ptr_t &device);

    model_subscription_t model_sub;
};

} // namespace syncspirit::fltk::tree_item
