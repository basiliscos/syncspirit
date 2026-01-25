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
#include "net/names.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using boost::nowide::narrow;

namespace bfs = std::filesystem;

namespace {

struct fixture_t;
struct supervisor_t : fs::fs_supervisor_t {
    using parent_t = fs::fs_supervisor_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.register_name(net::names::coordinator, get_address());
            p.register_name(net::names::db, get_address());
        });
        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&supervisor_t::on_watch);
            p.subscribe_actor(&supervisor_t::on_changes);
        });
    }

    void launch_children() noexcept override {
        // NOOP
    }

    void on_watch(message::watch_folder_t &) noexcept;
    void on_changes(message::folder_changes_t &) noexcept;

    fixture_t *fixture;
};

struct fixture_t {
    using watcher_actor_ptr_t = model::intrusive_ptr_t<fs::watch_actor_t>;
    using file_actor_ptr_t = model::intrusive_ptr_t<fs::file_actor_t>;
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
                         // .updates_mediator(updates_mediator)
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

    virtual void run() noexcept {
        fs_context.reset(new fs::fs_context_t());
        sup = fs_context->create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->fixture = this;

        create_updates_mediator();
        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        create_file_actor();
        create_watcher_actor();

        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        REQUIRE(static_cast<r::actor_base_t *>(watcher_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);

        auto folder_id = std::string("my-folder-id");
        auto back_addr = sup->get_address();
        auto ec = utils::make_error_code(utils::error_code_t::no_action);
        sup->route<fs::payload::watch_folder_t>(watcher_actor->get_address(), back_addr, root_path, folder_id, ec);
        sup->do_process();

        REQUIRE(watcher_replies == 1);
        main();

        sup->do_shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(watcher_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::SHUT_DOWN);
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void on_watch(message::watch_folder_t &msg) noexcept {
        CHECK(!msg.payload.ec);
        CHECK(msg.payload.ec.message() == "Success");
        ++watcher_replies;
    }

    virtual void on_changes(message::folder_changes_t &msg) noexcept { changes.emplace_back(&msg); }

    void poll() {
        changes.clear();
        fs_context->wait_next_event();
        fs_context->update_time();
        sup->do_process();
    }

    void make_dir() {
        auto path = root_path / L"папка";
        std::int64_t modified = 1641828421;

        auto conflict_path = bfs::path{};
        auto context = fs::payload::extendended_context_prt_t{};
        auto type = proto::FileInfoType::DIRECTORY;
        auto size = 0;
        auto deleted = false;
        auto perms = 0666;
        auto target = std::string();

        auto payload = fs::payload::remote_copy_t(std::move(context), path, conflict_path, type, size, perms, modified,
                                                  target, deleted, false);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(fs_addr, sup->get_address(), std::move(cmds));
    }

    virtual void main() noexcept {}

    r::pt::time_duration timeout = r::pt::millisec{10};
    r::pt::time_duration retension_timeout = r::pt::millisec{150};
    bfs::path root_path;
    test::path_guard_t path_guard;
    fs_context_ptr_r fs_context;
    fs::updates_mediator_ptr_t updates_mediator;
    r::intrusive_ptr_t<supervisor_t> sup;
    watcher_actor_ptr_t watcher_actor;
    file_actor_ptr_t file_actor;
    std::string_view folder_id = "1234-5678";
    utils::logger_t log;
    change_messages_t changes;
    r::address_ptr_t fs_addr;
    int watcher_replies = 0;
};

void supervisor_t::on_watch(message::watch_folder_t &msg) noexcept { fixture->on_watch(msg); }
void supervisor_t::on_changes(message::folder_changes_t &msg) noexcept { fixture->on_changes(msg); }

} // namespace

void test_without_mediator() {
    struct F : fixture_t {
        void main() noexcept override {
            make_dir();
            sup->do_process();
            poll();
            poll();
            REQUIRE(changes.size() == 1);
        }
    };
    F().run();
}

void test_with_mediator() {
    struct F : fixture_t {

        void create_file_actor() override {
            file_actor = sup->create_actor<fs::file_actor_t>()
                             .change_retension(retension_timeout * 2)
                             .updates_mediator(updates_mediator)
                             .timeout(timeout)
                             .finish();
            fs_addr = file_actor->get_address();
        }

        void main() noexcept override {
            make_dir();
            sup->do_process();
            poll();
            REQUIRE(changes.size() == 0);
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_without_mediator, "test_without_mediator", "[fs]");
    REGISTER_TEST_CASE(test_with_mediator, "test_with_mediator", "[fs]");
    return 1;
}

static int v = _init();
