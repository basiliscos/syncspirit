#include "app_supervisor.h"
#include "tree_item/devices.h"
#include "tree_item/folders.h"
#include "tree_item/ignored_devices.h"
#include "tree_item/peer_device.h"
#include "tree_item/pending_devices.h"
#include "tree_item/pending_folders.h"
#include "net/names.h"
#include "model/diff/load/load_cluster.h"
#include "model/diff/peer/cluster_update.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/add_pending_device.h"
#include "model/diff/modify/add_pending_folders.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/share_folder.h"
#include "utils/format.hpp"

#include <utility>
#include <sstream>
#include <iomanip>
#include <functional>

using namespace syncspirit::fltk;

namespace {
namespace resource {
r::plugin::resource_id_t model = 0;
}
} // namespace

db_info_viewer_guard_t::db_info_viewer_guard_t(app_supervisor_t *supervisor_) : supervisor{supervisor_} {}
db_info_viewer_guard_t::db_info_viewer_guard_t(db_info_viewer_guard_t &&other) { *this = std::move(other); }

db_info_viewer_guard_t &db_info_viewer_guard_t::operator=(db_info_viewer_guard_t &&other) {
    std::swap(supervisor, other.supervisor);
    return *this;
}

db_info_viewer_guard_t::~db_info_viewer_guard_t() {
    if (supervisor) {
        supervisor->db_info_viewer = nullptr;
    }
}

using callback_fn_t = std::function<void()>;

struct callback_impl_t final : callback_t {
    callback_impl_t(callback_fn_t fn_) : fn{std::move(fn_)} {}

    void eval() override { fn(); }

    callback_fn_t fn;
};

app_supervisor_t::app_supervisor_t(config_t &config)
    : parent_t(config), dist_sink(std::move(config.dist_sink)), config_path{std::move(config.config_path)},
      app_config(std::move(config.app_config)), content{nullptr}, devices{nullptr}, folders{nullptr},
      pending_devices{nullptr}, ignored_devices{nullptr}, db_info_viewer{nullptr} {
    started_at = clock_t::now();
    sequencer = model::make_sequencer(started_at.time_since_epoch().count());
}

auto app_supervisor_t::get_dist_sink() -> utils::dist_sink_t & { return dist_sink; }

auto app_supervisor_t::get_config_path() -> const bfs::path & { return config_path; }
auto app_supervisor_t::get_app_config() -> const config::main_t & { return app_config; }
auto app_supervisor_t::get_cluster() -> model::cluster_ptr_t & { return cluster; }
auto app_supervisor_t::get_sequencer() -> model::sequencer_t & { return *sequencer; }

void app_supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity("fltk", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link(false).callback([&](auto phase, auto &ee) {
            if (!ee && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&app_supervisor_t::on_model_update, coordinator);
                plugin->subscribe_actor(&app_supervisor_t::on_contact_update, coordinator);
                plugin->subscribe_actor(&app_supervisor_t::on_block_update, coordinator);
                plugin->subscribe_actor(&app_supervisor_t::on_io_error, coordinator);
                request<model::payload::model_request_t>(coordinator).send(init_timeout);
                resources->acquire(resource::model);
            }
        });
    });

    plugin.with_casted<r::plugin::starter_plugin_t>(
        [&](auto &p) {
            p.subscribe_actor(&app_supervisor_t::on_model_response);
            p.subscribe_actor(&app_supervisor_t::on_db_info_response);
        },
        r::plugin::config_phase_t::PREINIT);
}

void app_supervisor_t::on_model_response(model::message::model_response_t &res) noexcept {
    LOG_TRACE(log, "on_model_response");
    resources->release(resource::model);
    auto ee = res.payload.ee;
    if (ee) {
        LOG_ERROR(log, "cannot get model: {}", ee->message());
        return do_shutdown(ee);
    }
    cluster = std::move(res.payload.res.cluster);
}

void app_supervisor_t::on_model_update(model::message::model_update_t &message) noexcept {
    LOG_TRACE(log, "on_model_update");
    auto &diff = *message.payload.diff;
    auto r = diff.apply(*cluster);
    if (!r) {
        LOG_ERROR(log, "error applying cluster diff: {}", r.assume_error().message());
    }

    r = diff.visit(*this, nullptr);
    if (!r) {
        LOG_ERROR(log, "error visiting cluster diff: {}", r.assume_error().message());
    }
    auto custom = message.payload.custom;
    if (custom) {
        for (auto it = begin(callbacks); it != end(callbacks); ++it) {
            if (it->get() == custom) {
                auto cb = *it;
                callbacks.erase(it);
                cb->eval();
                break;
            }
        }
    }
}

void app_supervisor_t::on_contact_update(model::message::contact_update_t &message) noexcept {
    LOG_TRACE(log, "on_contact_update");
    auto &diff = *message.payload.diff;
    auto r = diff.apply(*cluster);
    if (!r) {
        auto ee = make_error(r.assume_error());
        LOG_ERROR(log, "error applying contact diff: {}", r.assume_error().message());
    }
    r = diff.visit(*this, nullptr);
    if (!r) {
        LOG_ERROR(log, "error visiting contact diff: {}", r.assume_error().message());
    }
}

void app_supervisor_t::on_block_update(model::message::block_update_t &message) noexcept {}

std::string app_supervisor_t::get_uptime() noexcept {
    using namespace std;
    using namespace std::chrono;
    auto uptime = clock_t::now() - started_at;
    auto h = duration_cast<hours>(uptime);
    uptime -= h;
    auto m = duration_cast<minutes>(uptime);
    uptime -= m;
    auto s = duration_cast<seconds>(uptime);
    uptime -= s;

    std::stringstream out;
    out.fill('0');
    out << setw(2) << h << ":" << setw(2) << m << ":" << setw(2) << s;

    return out.str();
}

void app_supervisor_t::on_db_info_response(net::message::db_info_response_t &res) noexcept {
    if (db_info_viewer) {
        auto &ee = res.payload.ee;
        if (ee) {
            log->warn("error requesting db info: {}", ee->message());
        } else {
            db_info_viewer->view(res.payload.res);
        }
    }
}

void app_supervisor_t::on_io_error(model::message::io_error_t &message) noexcept {
    for (auto &details : message.payload.errors) {
        log->warn("I/O error on '{}': {}", details.path.string(), details.ec.message());
    }
}

auto app_supervisor_t::get_logger() noexcept -> utils::logger_t & { return log; }
void app_supervisor_t::set_devices(tree_item_t *node) { devices = node; }
void app_supervisor_t::set_folders(tree_item_t *node) { folders = node; }
void app_supervisor_t::set_pending_devices(tree_item_t *node) { pending_devices = node; }
void app_supervisor_t::set_ignored_devices(tree_item_t *node) { ignored_devices = node; }

auto app_supervisor_t::request_db_info(db_info_viewer_t *viewer) -> db_info_viewer_guard_t {
    request<net::payload::db_info_request_t>(coordinator).send(init_timeout * 5 / 6);
    db_info_viewer = viewer;
    return db_info_viewer_guard_t(this);
}

callback_ptr_t app_supervisor_t::call_select_folder(std::string_view folder_id) {
    auto id = std::string(folder_id);
    auto fn = callback_fn_t([this, id = std::move(id)]() {
        auto folders_node = static_cast<tree_item::folders_t *>(folders);
        folders_node->select_folder(id);
    });
    auto cb = callback_ptr_t(new callback_impl_t(std::move(fn)));
    callbacks.push_back(cb);
    return cb;
}

callback_ptr_t app_supervisor_t::call_share_folders(std::string folder_id, std::vector<std::string> devices) {
    assert(devices.size());
    auto fn = callback_fn_t([this, folder_id = std::move(folder_id), devices = std::move(devices)]() {
        auto diff = model::diff::cluster_diff_ptr_t{};
        auto current = diff.get();
        for (auto &sha256 : devices) {
            auto device = cluster->get_devices().by_sha256(sha256);
            if (!device) {
                log->error("cannot share folder {}: target device is missing", folder_id);
                return;
            }
            auto folder = cluster->get_folders().by_id(folder_id);
            if (!folder) {
                log->error("cannot share folder {}: not such a folder", folder_id);
                return;
            }
            using diff_t = model::diff::modify::share_folder_t;
            auto opt = diff_t::create(*cluster, *sequencer, *device, *folder);
            if (!opt) {
                auto message = opt.assume_error().message();
                log->error("cannot share folder {} with {} : {}", folder_id, device->device_id(), message);
                return;
            }
            auto &sub_diff = opt.assume_value();
            if (!current) {
                diff = sub_diff;
                current = diff.get();
            } else {
                current = current->assign_sibling(sub_diff.get());
            }
        }
        auto cb = call_select_folder(folder_id);
        send_model<model::payload::model_update_t>(std::move(diff), cb.get());
    });
    auto cb = callback_ptr_t(new callback_impl_t(std::move(fn)));
    callbacks.push_back(cb);
    return cb;
}

auto app_supervisor_t::operator()(const model::diff::load::load_cluster_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto devices_node = static_cast<tree_item::devices_t *>(devices);

    auto &self_device = cluster->get_device();
    auto self_node = devices_node->set_self(*cluster->get_device());
    auto tree = self_node->get_owner()->tree();
    tree->select(self_node->get_owner());
    self_device->set_augmentation(*self_node);

    for (auto &it : cluster->get_devices()) {
        auto &device = *it.item;
        if (device.device_id() != cluster->get_device()->device_id()) {
            device.set_augmentation(devices_node->add_peer(device));
        }
    }

    auto pending_devices_node = static_cast<tree_item::pending_devices_t *>(pending_devices);
    for (auto &it : cluster->get_pending_devices()) {
        auto &device = *it.item;
        device.set_augmentation(pending_devices_node->add_device(device));
    }

    auto ignored_devices_node = static_cast<tree_item::ignored_devices_t *>(ignored_devices);
    for (auto &it : cluster->get_ignored_devices()) {
        auto &device = *it.item;
        device.set_augmentation(ignored_devices_node->add_device(device));
    }

    auto folders_node = static_cast<tree_item::folders_t *>(folders);
    for (auto &it : cluster->get_folders()) {
        auto &folder = it.item;
        auto augmentation = folders_node->add_folder(*folder);
        folder->set_augmentation(augmentation);
    }

    return diff.visit_next(*this, custom);
}

auto app_supervisor_t::operator()(const model::diff::modify::update_peer_t &diff, void *) noexcept
    -> outcome::result<void> {
    auto device = cluster->get_devices().by_sha256(diff.peer_id);
    auto augmentation = device->get_augmentation();
    if (!augmentation) {
        auto devices_node = static_cast<tree_item::devices_t *>(devices);
        device->set_augmentation(devices_node->add_peer(*device));
    }
    return outcome::success();
}

auto app_supervisor_t::operator()(const model::diff::modify::add_pending_folders_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &devices = cluster->get_devices();
    auto &pending_folders = cluster->get_pending_folders();
    for (auto &item : diff.container) {
        auto peer = devices.by_sha256(item.peer_id);
        auto augmentation = static_cast<augmentation_t *>(peer->get_augmentation().get());
        auto peer_node = static_cast<tree_item::peer_device_t *>(augmentation->get_owner());
        auto pending_node = static_cast<tree_item::pending_folders_t *>(peer_node->get_pending_folders());
        auto pending_folder = pending_folders.by_id(item.db.folder().id());
        if (pending_folder->get_augmentation()) {
            continue;
        }
        pending_folder->set_augmentation(pending_node->add_pending_folder(*pending_folder));
    }
    return diff.visit_next(*this, custom);
}

auto app_supervisor_t::operator()(const model::diff::modify::add_pending_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &device = *cluster->get_pending_devices().by_sha256(diff.device_id.get_sha256());
    auto pending_devices_node = static_cast<tree_item::pending_devices_t *>(pending_devices);
    device.set_augmentation(pending_devices_node->add_device(device));
    return diff.visit_next(*this, custom);
}

auto app_supervisor_t::operator()(const model::diff::modify::add_ignored_device_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &device = *cluster->get_ignored_devices().by_sha256(diff.device_id.get_sha256());
    auto ignored_devices_node = static_cast<tree_item::ignored_devices_t *>(ignored_devices);
    device.set_augmentation(ignored_devices_node->add_device(device));
    return diff.visit_next(*this, custom);
}

auto app_supervisor_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &folder = *cluster->get_folders().by_id(diff.db.id());
    if (!folder.get_augmentation()) {
        auto folders_node = static_cast<tree_item::folders_t *>(folders);
        auto augmentation = folders_node->add_folder(folder);
        folder.set_augmentation(augmentation);
    }
    return diff.visit_next(*this, custom);
}
