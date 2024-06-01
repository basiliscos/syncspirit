#pragma once

#pragma once

#include "../tree_item.h"

namespace syncspirit::fltk::tree_item {

struct devices_t : tree_item_t, private model_listener_t, private model::diff::cluster_visitor_t {
    using parent_t = tree_item_t;
    devices_t(app_supervisor_t &supervisor, Fl_Tree *tree);

    void on_select() override;
    void build_tree();
    void operator()(model::message::model_update_t &) override;
    outcome::result<void> operator()(const diff::load::load_cluster_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const diff::load::devices_t &, void *custom) noexcept override;
    outcome::result<void> operator()(const diff::modify::update_peer_t &, void *custom) noexcept override;

    tree_item_t *get_self_device();

    void add_device(const model::device_ptr_t &device);
    model_subscription_t model_sub;
};

} // namespace syncspirit::fltk::tree_item
