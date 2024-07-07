#include "ignored_devices.h"

#include "ignored_device.h"
#if 0
#include "model/diff/load/load_cluster.h"
#include "model/diff/contact/ignored_connected.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/remove_ignored_device.h"
#endif

using namespace syncspirit;
using namespace syncspirit::model::diff;
using namespace syncspirit::fltk;
using namespace syncspirit::fltk::tree_item;

ignored_devices_t::ignored_devices_t(app_supervisor_t &supervisor, Fl_Tree *tree) : parent_t(supervisor, tree) {
    update_label();
    tree->close(this, 0);
}

void ignored_devices_t::update_label() {
    auto cluster = supervisor.get_cluster();
    auto count = cluster ? cluster->get_ignored_devices().size() : 0;
    auto l = fmt::format("ignored devices ({})", count);
    label(l.data());
    tree()->redraw();
}

void ignored_devices_t::build_tree() {
    auto &cluster = supervisor.get_cluster();
    if (!cluster) {
        return;
    }
    clear_children();
    auto &devices = cluster->get_ignored_devices();

    tree()->begin();
    for (auto it : devices) {
        add_device(it.item);
    }
    tree()->end();
}

void ignored_devices_t::add_device(const model::ignored_device_ptr_t &device) {
    auto node = new ignored_device_t(device, supervisor, tree());
    add(prefs(), node->label(), node);
}

#if 0
void ignored_devices_t::operator()(model::message::model_update_t &update) {
    std::ignore = update.payload.diff->visit(*this, nullptr);
    build_tree();
    update_label();
}

auto ignored_devices_t::operator()(const diff::load::load_cluster_t &diff, void *data) noexcept
    -> outcome::result<void> {
    return diff.diff::cluster_aggregate_diff_t::visit(*this, data);
}

auto ignored_devices_t::operator()(const diff::load::ignored_devices_t &diff, void *) noexcept
    -> outcome::result<void> {
    build_tree();
    update_label();
    return outcome::success();
}

auto ignored_devices_t::operator()(const diff::contact::ignored_connected_t &diff, void *) noexcept
    -> outcome::result<void> {
    for (int i = 0; i < children(); ++i) {
        auto node = static_cast<ignored_device_t *>(child(i));
        if (node->device->get_device_id() == diff.device_id) {
            node->refresh();
            break;
        }
    }
    return outcome::success();
}

auto ignored_devices_t::operator()(const diff::modify::add_ignored_device_t &diff, void *) noexcept
    -> outcome::result<void> {
    auto &ignored_devices = supervisor.get_cluster()->get_ignored_devices();
    auto peer = ignored_devices.by_sha256(diff.device_id.get_sha256());
    add_device(peer);
    return outcome::success();
}

auto ignored_devices_t::operator()(const diff::modify::remove_ignored_device_t &diff, void *) noexcept
    -> outcome::result<void> {
    for (int i = 0; i < children(); ++i) {
        auto node = static_cast<ignored_device_t *>(child(i));
        if (node->device->get_device_id().get_sha256() == diff.get_device_sha256()) {
            node->select_other();
            break;
        }
    }
    return outcome::success();
}

auto ignored_devices_t::operator()(const diff::modify::update_peer_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    return diff.model::diff::cluster_aggregate_diff_t::visit(*this, custom);
}
#endif
