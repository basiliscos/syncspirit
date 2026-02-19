// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "test-utils.h"
#include "fs/fs_supervisor.h"
#include "fs/file_actor.h"
#include "fs/messages.h"
#include "fs/updates_mediator.h"
#include "fs/watcher_actor.h"
#include "fs/fs_context.h"
#include "net/local_keeper.h"
#include "net/names.h"
#include "model/cluster.h"
#include "diff-builder.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using namespace syncspirit::model;
using boost::nowide::narrow;

namespace bfs = std::filesystem;

namespace {

struct fixture_t;

struct supervisor_t : fs::fs_supervisor_t, model::diff::apply_controller_t, protected model::diff::cluster_visitor_t {
    using parent_t = fs::fs_supervisor_t;
    using model::diff::apply_controller_t::cluster;
    using parent_t::parent_t;
    using model::diff::cluster_visitor_t::operator();

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&supervisor_t::on_model_update); });
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.register_name(net::names::coordinator, get_address());
            p.register_name(net::names::db, get_address());
        });
    }

    void launch_children() noexcept override {
        // NOOP
    }

    virtual void on_model_update(model::message::model_update_t &msg) noexcept {
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

    model::sequencer_ptr_t sequencer;
    fixture_t *fixture;
};

struct fixture_t {
    using watcher_actor_ptr_t = model::intrusive_ptr_t<fs::watch_actor_t>;
    using file_actor_ptr_t = model::intrusive_ptr_t<fs::file_actor_t>;
    using local_keeper_ptr_t = model::intrusive_ptr_t<net::local_keeper_t>;
    using fs_context_ptr_r = r::intrusive_ptr_t<fs::fs_context_t>;
    using change_message_ptr_t = r::intrusive_ptr_t<fs::message::folder_changes_t>;
    using change_messages_t = std::deque<change_message_ptr_t>;

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} {
        log = utils::get_logger("fixture");
        bfs::create_directory(root_path);
    }

    virtual void create_updates_mediator() { updates_mediator = new fs::updates_mediator_t(retension_timeout * 2); }

    virtual void create_file_actor() {
        file_actor = sup->create_actor<fs::file_actor_t>()
                         .change_retension(retension_timeout * 2)
                         .updates_mediator(updates_mediator)
                         .timeout(timeout)
                         .finish();
        fs_addr = file_actor->get_address();
    }

    virtual void create_watcher_actor() {
        watcher_actor = sup->create_actor<watch_actor_t>()
                            .timeout(timeout)
                            .change_retension(retension_timeout)
                            .updates_mediator(updates_mediator)
                            .finish();
    }

    virtual void create_local_keeper() {
        local_keeper = sup->create_actor<net::local_keeper_t>()
                           .timeout(timeout)
                           .sequencer(sequencer)
                           .concurrent_hashes(10)
                           .files_scan_iteration_limit(100)
                           .watcher_impl(syncspirit_watcher_impl)
                           .finish();
    }

    void await_events(int count = 1) {
        for (int i = 0; i < count; ++i) {
            LOG_DEBUG(log, "awaiting events ({})", i);
            auto has_events = fs_context->wait_next_event();
            LOG_DEBUG(log, "has events = {}", has_events);
            fs_context->update_time();
            sup->do_process();
        }
    }

    virtual void run() noexcept {
        auto my_id_str = "KHQNO2S-5QSILRK-YX4JZZ4-7L77APM-QNVGZJT-EKU7IFI-PNEPBMY-4MXFMQD";
        auto my_id = device_id_t::from_string(my_id_str).value();
        local_device = device_t::create(my_id, "my-device").value();

        cluster = new cluster_t(local_device, 10);
        cluster->get_devices().put(local_device);
        sequencer = model::make_sequencer(1234);
        fs_context.reset(new fs::fs_context_t(retension_timeout * 3));

        sup = fs_context->create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->fixture = this;
        sup->cluster = cluster;
        sup->do_process();

        create_updates_mediator();
        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        create_file_actor();
        create_watcher_actor();
        create_local_keeper();
        sup->do_process();

        sup->send<syncspirit::model::payload::thread_ready_t>(sup->get_address(), cluster, std::this_thread::get_id());
        sup->send<syncspirit::model::payload::app_ready_t>(sup->get_address());
        sup->do_process();

        REQUIRE(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        REQUIRE(static_cast<r::actor_base_t *>(watcher_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        REQUIRE(static_cast<r::actor_base_t *>(local_keeper.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto folder_id = "1234-5678";
        auto builder = diff_builder_t(*cluster);
        builder.upsert_folder(folder_id, root_path, "folder-label", 0, true).apply(*sup);

        auto folder = cluster->get_folders().by_id(folder_id);
        REQUIRE(folder);
        local_folder = folder->get_folder_infos().by_device(*local_device);
        REQUIRE(folder);

        main();

        sup->do_shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(watcher_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::pt::time_duration retension_timeout = r::pt::millisec{150};
    cluster_ptr_t cluster;
    model::sequencer_ptr_t sequencer;
    model::device_ptr_t local_device;
    bfs::path root_path;
    test::path_guard_t path_guard;
    fs_context_ptr_r fs_context;
    fs::updates_mediator_ptr_t updates_mediator;
    r::intrusive_ptr_t<supervisor_t> sup;
    watcher_actor_ptr_t watcher_actor;
    file_actor_ptr_t file_actor;
    local_keeper_ptr_t local_keeper;
    std::string_view folder_id = "1234-5678";
    utils::logger_t log;
    r::address_ptr_t fs_addr;
    model::folder_info_ptr_t local_folder;
    int watcher_replies = 0;
};
} // namespace

void test_fs() {
    struct F : fixture_t {
        void main() noexcept override {
            bfs::create_directories(root_path / "a/b/c/d/e");
            await_events();
            auto &files = local_folder->get_file_infos();
            REQUIRE(files.size() == 5);
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_fs, "test_fs", "[fs][net]");
    return 1;
}

static int v = _init();
