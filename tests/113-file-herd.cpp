// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "test-utils.h"
#include "diff-builder.h"
#include "fs/fs_supervisor.h"
#include "fs/file_actor.h"
#include "fs/messages.h"
#include "fs/updates_mediator.h"
#include "fs/watcher_actor.h"
#include "fs/fs_context.h"
#include "net/local_keeper.h"
#include "net/names.h"
#include "hasher/hasher_actor.h"
#include "model/cluster.h"
#include "model/diff/modify/upsert_folder.h"
#include "model/diff/modify/upsert_folder_info.h"
#include "model/diff/advance/advance.h"
#include "presentation/folder_entity.h"
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

    outcome::result<void> operator()(const model::diff::modify::upsert_folder_t &diff, void *custom) noexcept override {
        auto folder_id = db::get_id(diff.db);
        auto folder = cluster->get_folders().by_id(folder_id);
        if (!folder->get_augmentation()) {
            auto folder_entity = presentation::folder_entity_ptr_t();
            folder_entity = new presentation::folder_entity_t(folder);
            folder->set_augmentation(folder_entity);
        }
        return diff.visit_next(*this, custom);
    }

    outcome::result<void> operator()(const model::diff::modify::upsert_folder_info_t &diff,
                                     void *custom) noexcept override {
        auto r = diff.visit_next(*this, custom);
        auto &folder = *cluster->get_folders().by_id(diff.folder_id);
        auto &device = *cluster->get_devices().by_sha256(diff.device_id);
        auto folder_info = folder.is_shared_with(device);
        if (&device != cluster->get_device()) {
            auto augmentation = folder.get_augmentation().get();
            auto folder_entity = static_cast<presentation::folder_entity_t *>(augmentation);
            folder_entity->on_insert(*folder_info);
        }
        return r;
    }

    outcome::result<void> operator()(const model::diff::advance::advance_t &diff, void *custom) noexcept override {
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
        return diff.visit_next(*this, custom);
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
        auto notify_watcher = [this](const fs::task::scan_dir_t &scan_dir) { watcher_actor->notify(scan_dir); };
        file_actor = sup->create_actor<fs::file_actor_t>()
                         .concurrent_hashes(1)
                         .change_retension(retension_timeout * 2)
                         .updates_mediator(updates_mediator)
                         .scan_dir_callback(notify_watcher)
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

        sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();
        create_watcher_actor();
        create_file_actor();
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
        REQUIRE(local_folder);
        local_files = &local_folder->get_file_infos();

        main();

        sup->do_shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(watcher_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::pt::time_duration retension_timeout = r::pt::millisec{15};
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
    model::file_infos_map_t *local_files;
};
} // namespace

void test_fs() {
    struct F : fixture_t {
        void main() noexcept override {
            int expected_events = test::wine_environment() ? 4 : 2;
            bfs::create_directories(root_path / "a/b/c/d/e");
            await_events(expected_events);
            REQUIRE(local_files->size() == 5);
            REQUIRE(local_files->by_name("a"));
            REQUIRE(local_files->by_name("a/b"));
            REQUIRE(local_files->by_name("a/b/c"));
            REQUIRE(local_files->by_name("a/b/c/d"));
            REQUIRE(local_files->by_name("a/b/c/d/e"));

            bfs::create_directories(root_path / L"a/b/c/подпапка");
            write_file(root_path / L"a/b/c/файлик.bin", "12345");
            await_events(expected_events);
            REQUIRE(local_files->by_name(narrow(L"a/b/c/подпапка")));
            auto file = local_files->by_name(narrow(L"a/b/c/файлик.bin"));
            REQUIRE(file);
            CHECK(file->get_size() == 5);

            auto long_name = "2026_Project_Report_Sales_Analysis_Financial_Quarter_One_Overview_Data_Insights_and_"
                             "Strategies_v1.0.pdf";
            bfs::create_directories(root_path / "a/xx");
            await_events(2);
            auto dir_1 = local_files->by_name("a/xx");
            REQUIRE(dir_1);

            bfs::rename(root_path / "a" / "xx", root_path / "a" / long_name);
            await_events(2);
            auto dir_2 = local_files->by_name(fmt::format("a/{}", long_name));
            REQUIRE(dir_2);

            bfs::rename(root_path / "a" / long_name, root_path / "a" / "yy");
            await_events(2);
            auto dir_3 = local_files->by_name("a/yy");
            REQUIRE(dir_3);
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
