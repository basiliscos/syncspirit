// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test_supervisor.h"
#include "model/diff/load/commit.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/advance/advance.h"
#include "model/diff/peer/update_folder.h"
#include "presentation/folder_entity.h"
#include "proto/proto-helpers-bep.h"
#include "proto/proto-helpers-db.h"
#include "net/names.h"

namespace to {
struct queue {};
struct on_timer_trigger {};
} // namespace to

template <> inline auto &rotor::supervisor_t::access<to::queue>() noexcept { return queue; }

namespace rotor {

template <>
inline auto rotor::actor_base_t::access<to::on_timer_trigger, request_id_t, bool>(request_id_t request_id,
                                                                                  bool cancelled) noexcept {
    on_timer_trigger(request_id, cancelled);
}

} // namespace rotor

using namespace syncspirit::net;
using namespace syncspirit::test;
using namespace syncspirit::presentation;

supervisor_t::supervisor_t(config_t &cfg) : parent_t(cfg) {
    auto_finish = cfg.auto_finish;
    auto_ack_io = cfg.auto_ack_io;
    make_presentation = cfg.make_presentation;
    configure_callback = cfg.configure_callback;
    sequencer = model::make_sequencer(1234);
}

void supervisor_t::configure(r::plugin::plugin_base_t &plugin) noexcept {
    parent_t::configure(plugin);
    plugin.with_casted<r::plugin::address_maker_plugin_t>([&](auto &p) {
        p.set_identity(std::string(names::coordinator) + ".test", false);
        log = utils::get_logger(identity);
    });
    plugin.with_casted<r::plugin::registry_plugin_t>(
        [&](auto &p) { p.register_name(names::coordinator, get_address()); });
    plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
        p.subscribe_actor(&supervisor_t::on_model_update);
        p.subscribe_actor(&supervisor_t::on_package);
        p.subscribe_actor(&supervisor_t::on_io);
    });
    if (configure_callback) {
        configure_callback(plugin);
    }
}

void supervisor_t::do_start_timer(const r::pt::time_duration &, r::timer_handler_base_t &handler) noexcept {
    timers.emplace_back(&handler);
}

void supervisor_t::do_cancel_timer(r::request_id_t timer_id) noexcept {
    auto it = timers.begin();
    while (it != timers.end()) {
        auto &handler = *it;
        if (handler->request_id == timer_id) {
            auto &actor_ptr = handler->owner;
            actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, true);
            on_timer_trigger(timer_id, true);
            timers.erase(it);
            return;
        } else {
            ++it;
        }
    }
    assert(0 && "should not happen");
}

void supervisor_t::do_invoke_timer(r::request_id_t timer_id) noexcept {
    LOG_DEBUG(log, "{}, invoking timer {}", identity, timer_id);
    auto predicate = [&](auto &handler) { return handler->request_id == timer_id; };
    auto it = std::find_if(timers.begin(), timers.end(), predicate);
    assert(it != timers.end());
    auto &handler = *it;
    auto &actor_ptr = handler->owner;
    actor_ptr->access<to::on_timer_trigger, r::request_id_t, bool>(timer_id, false);
    timers.erase(it);
}

void supervisor_t::start() noexcept {}
void supervisor_t::shutdown() noexcept { do_shutdown(); }

void supervisor_t::enqueue(r::message_ptr_t message) noexcept {
    locality_leader->access<to::queue>().emplace_back(std::move(message));
}

void supervisor_t::on_model_update(model::message::model_update_t &msg) noexcept {
    LOG_TRACE(log, "updating model");
    auto &diff = msg.payload.diff;
    auto r = diff->apply(*this, {});
    if (!r) {
        LOG_ERROR(log, "error updating model: {}", r.assume_error().message());
        do_shutdown(make_error(r.assume_error()));
    }

    r = diff->visit(*this, nullptr);
    if (!r) {
        LOG_ERROR(log, "error visiting model: {}", r.assume_error().message());
        do_shutdown(make_error(r.assume_error()));
    }
}

void supervisor_t::on_package(bouncer::message::package_t &msg) noexcept {
    LOG_TRACE(log, "on package");
    put(std::move(msg.payload));
}

void supervisor_t::on_io(fs::message::io_commands_t &message) noexcept {
    for (auto &cmd : message.payload.commands) {
        std::visit([&](auto &cmd) { process_io(cmd); }, cmd);
    }
}

auto supervisor_t::consume_errors() noexcept -> io_errors_t { return std::move(io_errors); }

auto supervisor_t::operator()(const model::diff::local::io_failure_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto &errs = diff.errors;
    std::copy(errs.begin(), errs.end(), std::back_inserter(io_errors));
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (make_presentation) {
        auto folder_id = db::get_id(diff.db);
        auto folder = cluster->get_folders().by_id(folder_id);
        if (!folder->get_augmentation()) {
            auto folder_entity = folder_entity_ptr_t(new folder_entity_t(folder));
            folder->set_augmentation(folder_entity);
        }
    }
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::modify::upsert_folder_info_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    auto r = diff.visit_next(*this, custom);
    if (make_presentation) {
        auto &folder = *cluster->get_folders().by_id(diff.folder_id);
        auto &device = *cluster->get_devices().by_sha256(diff.device_id);
        auto folder_info = folder.is_shared_with(device);
        if (&device != cluster->get_device()) {
            auto augmentation = folder.get_augmentation().get();
            auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
            folder_entity->on_insert(*folder_info);
        }
    }
    return r;
}

auto supervisor_t::operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (make_presentation) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto augmentation = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
        if (folder_entity) {
            auto &folder_infos = folder->get_folder_infos();
            auto local_fi = folder_infos.by_device(*cluster->get_device());
            auto file_name = proto::get_name(diff.proto_local);
            auto local_file = local_fi->get_file_infos().by_name(file_name);
            if (local_file) {
                folder_entity->on_insert(*local_file, *local_fi);
            }
        }
    }
    return diff.visit_next(*this, custom);
}

auto supervisor_t::operator()(const model::diff::peer::update_folder_t &diff, void *custom) noexcept
    -> outcome::result<void> {
    if (make_presentation) {
        auto folder = cluster->get_folders().by_id(diff.folder_id);
        auto folder_aug = folder->get_augmentation().get();
        auto folder_entity = static_cast<presentation::folder_entity_t *>(folder_aug);

        auto &devices_map = cluster->get_devices();
        auto peer = devices_map.by_sha256(diff.peer_id);
        auto folder_info = folder->get_folder_infos().by_device(*peer);
        auto &files_map = folder_info->get_file_infos();

        for (auto &file : diff.files) {
            auto file_name = proto::get_name(file);
            auto file_info = files_map.by_name(file_name);
            auto augmentation = file_info->get_augmentation().get();
            if (!augmentation) {
                folder_entity->on_insert(*file_info, *folder_info);
            }
        }
    }
    return diff.visit_next(*this, custom);
}

void supervisor_t::process_io(fs::payload::block_request_t &req) noexcept {
    LOG_TRACE(log, "process_io, requesting on '{}' (offset: {}, size: {})", req.path.string(), req.offset,
              req.block_size);
}

void supervisor_t::process_io(fs::payload::remote_copy_t &req) noexcept {
    LOG_TRACE(log, "process_io (ack: {}), remote_copy_t of '{} ({} bytes)'", auto_ack_io, req.path.string(), req.size);
    if (auto_ack_io) {
        req.result = outcome::success();
    }
}

void supervisor_t::process_io(fs::payload::append_block_t &req) noexcept {
    LOG_TRACE(log, "process_io (ack: {}), append_block_t of {}", auto_ack_io, req.path.string());
    if (auto_ack_io) {
        req.result = outcome::success();
    }
}

void supervisor_t::process_io(fs::payload::finish_file_t &req) noexcept {
    LOG_TRACE(log, "process_io (ack: {}), finish_file_t of {}", auto_ack_io, req.path.string());
    if (auto_ack_io) {
        req.result = outcome::success();
    }
}

void supervisor_t::process_io(fs::payload::clone_block_t &req) noexcept {
    LOG_TRACE(log, "process_io (ack: {}), clone_block_t, {} bytes,  {}(#{}) -> {}(#{})", auto_ack_io, req.block_size,
              req.source.string(), req.source_offset, req.target.string(), req.target_offset);
    if (auto_ack_io) {
        req.result = outcome::success();
    }
}

auto supervisor_t::apply(const model::diff::load::commit_t &message, void *) noexcept -> outcome::result<void> {
    put(message.commit_message);
    return outcome::success();
}
