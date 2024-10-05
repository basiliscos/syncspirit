#include "app_supervisor.h"
#include "main_window.h"
#include "tree_item/devices.h"
#include "tree_item/folders.h"
#include "tree_item/ignored_devices.h"
#include "tree_item/peer_device.h"
#include "tree_item/peer_folder.h"
#include "tree_item/pending_devices.h"
#include "tree_item/pending_folders.h"
#include "tree_item/peer_folders.h"
#include "tree_item/virtual_entry.h"
#include "net/names.h"
#include "config/utils.h"
#include "model/diff/load/load_cluster.h"
#include "model/diff/modify/add_ignored_device.h"
#include "model/diff/modify/add_pending_device.h"
#include "model/diff/modify/add_pending_folders.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/modify/update_peer.h"
#include "model/diff/modify/share_folder.h"
#include "model/diff/peer/update_folder.h"
#include "utils/format.hpp"

#include <utility>
#include <sstream>
#include <fstream>
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
      app_config(std::move(config.app_config)), app_config_original{app_config}, content{nullptr}, devices{nullptr},
      folders{nullptr}, pending_devices{nullptr}, ignored_devices{nullptr}, db_info_viewer{nullptr},
      main_window{nullptr} {
    started_at = clock_t::now();
    sequencer = model::make_sequencer(started_at.time_since_epoch().count());
}

auto app_supervisor_t::get_dist_sink() -> utils::dist_sink_t & { return dist_sink; }

auto app_supervisor_t::get_config_path() -> const bfs::path & { return config_path; }
auto app_supervisor_t::get_app_config() -> config::main_t & { return app_config; }
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

void app_supervisor_t::shutdown_finish() noexcept {
    parent_t::shutdown_finish();
    LOG_TRACE(log, "shutdown_finish");
    if (main_window) {
        main_window->on_shutdown();
    }
    std::stringstream out;
    std::stringstream out_orig;
    auto r = config::serialize(app_config, out);
    auto r_orig = config::serialize(app_config_original, out_orig);
    if (r.has_value() && r_orig.has_value()) {
        if (out.str() != out_orig.str()) {
            write_config(app_config);
        }
    }
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
void app_supervisor_t::set_main_window(main_window_t *window) { main_window = window; }

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
    if (!devices) {
        return diff.visit_next(*this, custom);
    }
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

auto app_supervisor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = diff.visit_next(*this, custom);
    auto &folder = *cluster->get_folders().by_id(diff.folder_id);
    auto &device = *cluster->get_devices().by_sha256(diff.device_id);
    if (&device != cluster->get_device()) {
        auto folder_info = folder.is_shared_with(device);
        auto devices_node = static_cast<tree_item::devices_t *>(devices);
        auto peer_node = devices_node->get_peer(device);
        auto folders_node = static_cast<tree_item::peer_folders_t *>(peer_node->get_folders());
        if (!folder_info->get_augmentation()) {
            folders_node->add_folder(*folder_info);
        }
    }
    return r;
}

auto app_supervisor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto folder = cluster->get_folders().by_id(diff.folder_id);
    auto peer = cluster->get_devices().by_sha256(diff.peer_id);
    auto folder_info = folder->get_folder_infos().by_device(*peer);
    if (auto generic_augmnetation = folder_info->get_augmentation(); generic_augmnetation) {
        auto augmentation = static_cast<augmentation_t *>(generic_augmnetation.get());
        auto folder_entry = dynamic_cast<tree_item::peer_folder_t *>(augmentation->get_owner());
        if (folder_entry->expandend) {
            auto &files_map = folder_info->get_file_infos();
            for (auto &file : diff.files) {
                auto file_info = files_map.by_name(file.name());
                if (!file_info->get_augmentation()) {
                    auto path = bfs::path(file_info->get_name());
                    auto dir = folder_entry->locate_dir(path.parent_path());
                    dir->add_entry(*file_info);
                }
            }
        }
    }
    return diff.visit_next(*this, custom);
}

void app_supervisor_t::write_config(const config::main_t &cfg) noexcept {
    log->debug("going to write config");
    auto &path = get_config_path();
    std::fstream f_cfg(path, f_cfg.binary | f_cfg.trunc | f_cfg.in | f_cfg.out);
    auto r = config::serialize(cfg, f_cfg);
    if (!r) {
        log->error("cannot save default config at {}: {}", path, r.error().message());
    } else {
        log->info("succesfully stored config at {}. Restart to apply", path);
    }
    app_config_original = app_config = cfg;
}

void app_supervisor_t::set_show_deleted(bool value) {
    log->debug("display deleted = {}", value);
    app_config.fltk_config.display_deleted = value;

    auto &self = cluster->get_device();
    for (auto &it_f : cluster->get_folders()) {
        for (auto &it : it_f.item->get_folder_infos()) {
            if (it.item->get_device() != self.get()) {
                auto generic_augmnetation = it.item->get_augmentation();
                if (generic_augmnetation) {
                    auto augmentation = static_cast<augmentation_t *>(generic_augmnetation.get());
                    auto virtual_entry = dynamic_cast<tree_item::virtual_entry_t *>(augmentation->get_owner());
                    if (virtual_entry) {
                        virtual_entry->show_deleted(value);
                    }
                }
            }
        }
    }
}
