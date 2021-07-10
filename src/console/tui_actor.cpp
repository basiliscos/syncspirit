#include "tui_actor.h"
#include "utils.h"
#include "../net/names.h"
#include "../constants.h"
#include "../utils/sink.h"
#include "config_activity.h"
#include "default_activity.h"
#include "new_folder_activity.h"
#include "peer_activity.h"

using namespace syncspirit::console;
using sink_t = syncspirit::utils::sink_t;

namespace {
namespace resource {
r::plugin::resource_id_t tty = 0;
}
} // namespace

static const char NAME[] = "tui";
const char *tui_actor_t::progress = "|/-\\";

tui_actor_t::tui_actor_t(config_t &cfg)
    : r::actor_base_t{cfg}, strand{static_cast<ra::supervisor_asio_t *>(cfg.supervisor)->get_strand()},
      interactive{cfg.interactive}, mutex{cfg.mutex}, prompt{cfg.prompt}, tui_config{cfg.tui_config} {

    if (!console::install_signal_handlers()) {
        spdlog::critical("signal handlers cannot be installed");
        throw std::runtime_error("signal handlers cannot be installed");
    }

    if (interactive) {
        progress_last = strlen(progress);
        tty = std::make_unique<tty_t::element_type>(strand.context(), STDIN_FILENO);
        push_activity(std::make_unique<default_activity_t>(*this));
    }
}

void tui_actor_t::on_start() noexcept {
    spdlog::debug("{}, on_start", identity);
    r::actor_base_t::on_start();
    start_timer();
    if (interactive) {
        do_read();
    }
}

void tui_actor_t::shutdown_start() noexcept {
    spdlog::debug("{}, shutdown_start", identity);
    r::actor_base_t::shutdown_start();
    supervisor->shutdown();
    if (coordinator) {
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        send<r::payload::shutdown_trigger_t>(coordinator, coordinator, make_error(ec));
    }
    if (resources->has(resource::tty)) {
        tty->cancel();
    }
}

void tui_actor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    r::actor_base_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) { p.set_identity(NAME, false); });
    plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
        p.discover_name(net::names::coordinator, coordinator, true).link().callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&tui_actor_t::on_discovery, coordinator);
                plugin->subscribe_actor(&tui_actor_t::on_auth, coordinator);
            }
        });
        p.discover_name(net::names::cluster, cluster, true).link().callback([&](auto phase, auto &ec) {
            if (!ec && phase == r::plugin::registry_plugin_t::phase_t::linking) {
                auto p = get_plugin(r::plugin::starter_plugin_t::class_identity);
                auto plugin = static_cast<r::plugin::starter_plugin_t *>(p);
                plugin->subscribe_actor(&tui_actor_t::on_new_folder, cluster);
            }
        });
    });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&tui_actor_t::on_config);
        p.subscribe_actor(&tui_actor_t::on_config_save);
        p.subscribe_actor(&tui_actor_t::on_create_folder);
        p.subscribe_actor(&tui_actor_t::on_ignrore_device);
        p.subscribe_actor(&tui_actor_t::on_update_peer);
        p.subscribe_actor(&tui_actor_t::on_ignrore_folder);
        assert(coordinator);
        auto timeout = init_timeout / 2;
        request<ui::payload::config_request_t>(coordinator).send(timeout);
    });
}

void tui_actor_t::start_timer() noexcept {
    auto interval = r::pt::milliseconds{tui_config.refresh_interval};
    timer_id = r::actor_base_t::start_timer(interval, *this, &tui_actor_t::on_timer);
}

void tui_actor_t::do_read() noexcept {
    if (state < r::state_t::SHUTTING_DOWN) {
        resources->acquire(resource::tty);
        auto fwd = ra::forwarder_t(*this, &tui_actor_t::on_read, &tui_actor_t::on_read_error);
        asio::mutable_buffer buff(input, 1);
        asio::async_read(*tty, buff, std::move(fwd));
    }
}

void tui_actor_t::on_read(size_t) noexcept {
    resources->release(resource::tty);
    input[1] = 0;
    auto k = input[0];
    if (!activities.front()->handle(k)) {
        if (k == tui_config.key_quit) {
            action_quit();
        } else if (k == tui_config.key_more_logs) {
            action_more_logs();
        } else if (k == tui_config.key_less_logs) {
            action_less_logs();
        } else if (k == tui_config.key_config) {
            action_config();
        } else if (k == 27) { /* escape */
            action_esc();
        }
    }
    do_read();
}

void tui_actor_t::on_read_error(const sys::error_code &ec) noexcept {
    resources->release(resource::tty);
    if (ec != asio::error::operation_aborted) {
        spdlog::error("{}, on_read_error, stdin reading error :: {}", identity, ec.message());
        do_shutdown(make_error(ec));
    }
}

void tui_actor_t::on_timer(r::request_id_t, bool) noexcept {
    if (console::shutdown_flag) {
        auto ec = r::make_error_code(r::shutdown_code_t::normal);
        return do_shutdown(make_error(ec));
    }
    if (interactive) {
        if (console::reset_term_flag) {
            console::term_prepare();
            tty->non_blocking(true);
            console::reset_term_flag = false;
        }

        flush_prompt();
    }
    start_timer();
}

void tui_actor_t::set_prompt(const std::string &value) noexcept {
    prompt_buff = value;
    flush_prompt();
}

void tui_actor_t::push_activity(activity_ptr_t &&activity) noexcept {
    if (!interactive) {
        return;
    }

    auto predicate = [&](auto &it) { return *it == *activity; };
    auto count = std::count_if(activities.begin(), activities.end(), predicate);
    // skip duplicates
    if (count != 0) {
        return;
    }
    auto it = activities.begin();
    while (it != activities.end()) {
        auto &prev = *it;
        if (!prev->locked()) {
            break;
        }
        ++it;
    }

    bool display = it == activities.begin();
    ++activities_count;
    activities.insert(it, std::move(activity));
    if (display) {
        activities.front()->display();
    }
}

void tui_actor_t::postpone_activity() noexcept {
    if (!interactive) {
        return;
    }
    if (activities_count > 1) {
        auto a = std::move(activities.front());
        activities.pop_front();
        activities.emplace_back(std::move(a));
        activities.front()->display();
    }
}

void tui_actor_t::discard_activity() noexcept {
    if (!interactive) {
        return;
    }
    --activities_count;
    activities.pop_front();
    activities.front()->display();
}

void tui_actor_t::action_quit() noexcept {
    spdlog::info("{}, action_quit", identity);
    console::shutdown_flag = true;
}

void tui_actor_t::action_more_logs() noexcept {
    auto level = spdlog::default_logger_raw()->level();
    auto l = static_cast<int>(level);
    if (l > 0) {
        spdlog::set_level(static_cast<decltype(level)>(--l));
        spdlog::info("{}, action_more_logs, applied ({})", identity, l);
    }
}

void tui_actor_t::action_less_logs() noexcept {
    auto level = spdlog::default_logger_raw()->level();
    auto l = static_cast<int>(level);
    auto m = static_cast<int>(decltype(level)::critical);
    if (l < m) {
        spdlog::info("{}, action_less_logs, applied ({})", identity, l);
        spdlog::set_level(static_cast<decltype(level)>(++l));
    }
}

void tui_actor_t::action_esc() noexcept { activities.front()->forget(); }

void tui_actor_t::action_config() noexcept {
    push_activity(std::make_unique<config_activity_t>(*this, app_config, app_config_orig));
}

void tui_actor_t::save_config() noexcept {
    auto timeout = init_timeout / 2;
    request<ui::payload::config_save_request_t>(coordinator, app_config).send(timeout);
}

void tui_actor_t::on_discovery(ui::message::discovery_notify_t &message) noexcept {
    push_activity(std::make_unique<peer_activity_t>(*this, message));
}

void tui_actor_t::on_auth(ui::message::auth_notify_t &message) noexcept {
    push_activity(std::make_unique<peer_activity_t>(*this, message));
}

void tui_actor_t::on_new_folder(ui::message::new_folder_notify_t &message) noexcept {
    push_activity(std::make_unique<new_folder_activity_t>(*this, message));
}

void tui_actor_t::on_config(ui::message::config_response_t &message) noexcept {
    app_config_orig = app_config = message.payload.res;
}

void tui_actor_t::on_config_save(ui::message::config_save_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    if (ee) {
        spdlog::error("{}, cannot save config: {}", identity, ee->message());
        return;
    }
    spdlog::trace("{}, on_config_save", identity);
    app_config_orig = app_config;
}

void tui_actor_t::on_ignrore_device(ui::message::ignore_device_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &device = message.payload.req->payload.request_payload.device;
    if (ee) {
        spdlog::error("{}, cannot ignore device {}: {}", identity, *device, ee->message());
        return;
    }
    spdlog::info("{}, device '{}' marked as ignored", identity, *device);
}

void tui_actor_t::on_update_peer(ui::message::update_peer_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &peer = message.payload.req->payload.request_payload.peer;
    if (ee) {
        spdlog::error("{}, cannot update peer {}: {}", identity, peer->device_id, ee->message());
        return;
    }
    spdlog::info("{}, peerd device '{}' has been added", identity, peer->device_id);
}

void tui_actor_t::on_ignrore_folder(ui::message::ignore_folder_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &folder = message.payload.req->payload.request_payload.folder;
    if (ee) {
        spdlog::error("{}, cannot ignore folder {}: {}", identity, folder->id, ee->message());
        return;
    }
    spdlog::info("{}, folder '{}/{}' marked as ignored", identity, folder->id, folder->label);
}

void tui_actor_t::on_create_folder(ui::message::create_folder_response_t &message) noexcept {
    auto &ee = message.payload.ee;
    auto &f = message.payload.req->payload.request_payload.folder;
    if (ee) {
        spdlog::error("{}, cannot create folder {}: {}", identity, f.id(), ee->message());
        return;
    }
    auto &folder = message.payload.res.folder;
    spdlog::info("{}, folder '{}/{}' has been created", identity, folder->id(), folder->label());
}

void tui_actor_t::ignore_device(const model::device_id_t &device_id) noexcept {
    auto timeout = init_timeout / 2;
    auto device = model::ignored_device_ptr_t(new model::device_id_t(device_id));
    request<ui::payload::ignore_device_request_t>(coordinator, device).send(timeout);
}

void tui_actor_t::ignore_folder(const proto::Folder &folder, model::device_ptr_t &source) noexcept {
    auto folder_db = db::IgnoredFolder();
    folder_db.set_id(folder.id());
    folder_db.set_label(folder.label());
    auto f = model::ignored_folder_ptr_t(new model::ignored_folder_t(folder_db));
    request<ui::payload::ignore_folder_request_t>(coordinator, f).send(init_timeout);
}

void tui_actor_t::create_folder(const proto::Folder &folder, model::device_ptr_t &source, std::uint64_t source_index,
                                const std::string &path) noexcept {
    db::Folder f;
    f.set_path(path);
    f.set_id(folder.id());
    f.set_label(folder.label());
    f.set_read_only(folder.read_only());
    f.set_ignore_permissions(folder.ignore_permissions());
    f.set_ignore_delete(folder.ignore_delete());
    f.set_disable_temp_indexes(folder.disable_temp_indexes());
    f.set_paused(false);
    f.set_watched(true);
    f.set_folder_type(db::FolderType::send_and_receive);
    f.set_pull_order(db::PullOrder::random);
    f.set_rescan_interval(3600);
    request<ui::payload::create_folder_request_t>(cluster, f, source, source_index).send(init_timeout);
}

void tui_actor_t::update_device(model::device_ptr_t device) noexcept {
    auto timeout = init_timeout / 2;
    request<ui::payload::update_peer_request_t>(coordinator, device).send(timeout);
}

void tui_actor_t::flush_prompt() noexcept {
    if (!interactive) {
        return;
    }

    char c;
    if (progress_idx < progress_last) {
        c = progress[progress_idx];
        ++progress_idx;
    } else {
        c = progress[progress_idx = 0];
    }
    auto r = fmt::format("\r\033[2K[{}{}{}{}] {}", sink_t::bold, sink_t::cyan, std::string_view(&c, 1), sink_t::reset,
                         prompt_buff);
    std::lock_guard<std::mutex> lock(*mutex);
    *prompt = r;
    fwrite(prompt->data(), sizeof(char), prompt->size(), stdout);
    fflush(stdout);
}
