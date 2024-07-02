#include "unknown_devices.h"

#include "unknown_device.h"
#include "model/diff/load/load_cluster.h"
#include "model/diff/contact/unknown_connected.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/remove_unknown_device.h"

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

unknown_devices_t::unknown_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree)
    : parent_t(supervisor, tree), model_sub(supervisor.add(this)) {
    update_label();
}

void unknown_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_unknown_devices().size() : 0;
    auto l = fmt::format("unknown devices ({})", count);
    label(l.data());
    tree()->redraw();
}

void unknown_devices_t::build_tree() {
    auto &cluster = supervisor.get_cluster();
    if (!cluster) {
        return;
    }
    clear_children();
    auto &devices = cluster->get_unknown_devices();

    tree()->begin();
    for (auto it : devices) {
        add_device(it.item);
    }
    tree()->end();
}

void unknown_devices_t::add_device(const model::unknown_device_ptr_t &device) {
    auto node = new unknown_device_t(device, supervisor, tree());
    add(prefs(), node->label(), node);
}

void unknown_devices_t::operator()(model::message::model_update_t &update) {
    std::ignore = update.payload.diff->visit(*this, nullptr);
    build_tree();
    update_label();
}

void unknown_devices_t::operator()(model::message::contact_update_t &diff) {
    std::ignore = diff.payload.diff->visit(*this, nullptr);
}

auto unknown_devices_t::operator()(const diff::load::load_cluster_t &diff, void *data) noexcept
    -> outcome::result<void> {
    return diff.diff::cluster_aggregate_diff_t::visit(*this, data);
}

auto unknown_devices_t::operator()(const diff::load::unknown_devices_t &diff, void *) noexcept
    -> outcome::result<void> {
    build_tree();
    update_label();
    return outcome::success();
}

auto unknown_devices_t::operator()(const diff::contact::unknown_connected_t &diff, void *) noexcept
    -> outcome::result<void> {
    for (int i = 0; i < children(); ++i) {
        auto node = static_cast<unknown_device_t *>(child(i));
        if (node->device->get_device_id() == diff.device_id) {
            node->refresh();
            break;
        }
    }
    return outcome::success();
}

auto unknown_devices_t::operator()(const diff::modify::remove_unknown_device_t &diff, void *) noexcept
    -> outcome::result<void> {
    for (int i = 0; i < children(); ++i) {
        auto node = static_cast<unknown_device_t *>(child(i));
        if (node->device->get_device_id().get_sha256() == diff.get_device_sha256()) {
            node->select_other();
            break;
        }
    }
    return outcome::success();
}

auto unknown_devices_t::operator()(const diff::modify::update_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.model::diff::cluster_aggregate_diff_t::visit(*this, custom);
}

auto unknown_devices_t::operator()(const diff::modify::add_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.model::diff::cluster_aggregate_diff_t::visit(*this, custom);
}
