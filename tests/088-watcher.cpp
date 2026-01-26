// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Ivan Baidakou

#include "access.h"
#include "test-utils.h"
#include "test-watcher.h"
#include "fs/fs_context.h"
#include "fs/fs_supervisor.h"
#include "fs/watcher_actor.h"
#include "utils/error_code.h"
#include "net/names.h"
#include "syncspirit-config.h"
#include <deque>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::fs;
namespace bfs = std::filesystem;
using boost::nowide::narrow;

struct fixture_t;

struct supervisor_t : fs::fs_supervisor_t {
    using parent_t = fs::fs_supervisor_t;
    using parent_t::parent_t;

    void configure(r::plugin::plugin_base_t &plugin) noexcept override {
        parent_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.register_name(net::names::coordinator, get_address()); });
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
    using target_ptr_t = r::intrusive_ptr_t<fs::watch_actor_t>;
    using fs_context_ptr_r = r::intrusive_ptr_t<fs::fs_context_t>;
    using change_message_ptr_t = r::intrusive_ptr_t<fs::message::folder_changes_t>;
    using change_messages_t = std::deque<change_message_ptr_t>;

    fixture_t(bool auto_launch_ = true) noexcept
        : auto_launch{auto_launch_}, root_path{unique_path()}, path_guard{root_path} {
        bfs::create_directory(root_path);
    }

    void run() noexcept {
        fs_context.reset(new fs::fs_context_t());
        sup = fs_context->create_supervisor<supervisor_t>().timeout(timeout).create_registry().finish();
        sup->fixture = this;

        sup->start();
        sup->do_process();
        REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        updates_mediator = new fs::updates_mediator_t(retension() * 2);
        if (auto_launch) {
            launch_target();
            REQUIRE(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }

        main();

        sup->do_process();
        sup->do_shutdown();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void launch_target() {
        target = sup->create_actor<target_ptr_t::element_type>()
                     .timeout(timeout)
                     .change_retension(retension())
                     .updates_mediator(updates_mediator)
                     .finish();
        sup->do_process();
    }

    void poll() {
        changes.clear();
        fs_context->wait_next_event();
        fs_context->update_time();
        sup->do_process();
    }

    virtual void on_watch(message::watch_folder_t &msg) noexcept {
        CHECK(!msg.payload.ec);
        CHECK(msg.payload.ec.message() == "Success");
        ++watched_replies;
    }

    virtual void on_changes(message::folder_changes_t &msg) noexcept { changes.emplace_back(&msg); }

    virtual void main() noexcept {}

    r::pt::time_duration retension() { return r::pt::microseconds{1}; }

    bool auto_launch;
    bfs::path root_path;
    test::path_guard_t path_guard;
    fs_context_ptr_r fs_context;
    r::intrusive_ptr_t<supervisor_t> sup;
    fs::updates_mediator_ptr_t updates_mediator;
    target_ptr_t target;
    r::pt::time_duration timeout = r::pt::millisec{10};
    change_messages_t changes;
    size_t watched_replies = 0;
};

void supervisor_t::on_watch(message::watch_folder_t &msg) noexcept { fixture->on_watch(msg); }
void supervisor_t::on_changes(message::folder_changes_t &msg) noexcept { fixture->on_changes(msg); }

void test_watcher_base() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void launch_target() override {
            target = sup->create_actor<test::test_watcher_t>()
                         .timeout(timeout)
                         .change_retension(retension())
                         .updates_mediator(updates_mediator)
                         .finish();
            sup->do_process();
        }

        void main() noexcept override {
            using U = fs::update_type_t;
            auto folder_id = std::string("my-folder-id");
            auto back_addr = sup->get_address();
            auto ec = utils::make_error_code(utils::error_code_t::no_action);
            sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
            sup->do_process();
            REQUIRE(watched_replies == 1);

            auto deadline = r::pt::microsec_clock::local_time() + retension();
            SECTION("simple (creation)") {
                SECTION("dir") {
                    auto own_name = bfs::path(L"папка");
                    auto sub_path = root_path / own_name;
                    bfs::create_directories(sub_path);
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                    CHECK(proto::get_size(file_change) == 0);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::DIRECTORY);
                    CHECK(proto::get_permissions(file_change));
                }
                SECTION("file") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                }
#ifndef SYNCSPIRIT_WIN
                SECTION("symlink") {
                    auto own_name = bfs::path(L"ссылка");
                    auto sub_path = root_path / own_name;
                    auto where = bfs::path("/to/some/where");
                    bfs::create_symlink(where, sub_path);
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                    CHECK(proto::get_size(file_change) == 0);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::SYMLINK);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(proto::get_symlink_target(file_change) == narrow(where.wstring()));
                }
#endif
            }
            SECTION("changes accumulation") {
                auto deadline_2 = deadline + retension();
                SECTION("simple case") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), U::content);
                    poll();
                    REQUIRE(changes.size() == 0);
                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(file_change.update_reason == update_type_t::created);
                }
                SECTION("create , delete -> collapse to void") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), U::deleted);
                    poll();
                    REQUIRE(changes.size() == 0);
                    poll();
                    REQUIRE(changes.size() == 0);
                }
                SECTION("content , meta -> collapse to content") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), U::content);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), U::meta);
                    poll();
                    REQUIRE(changes.size() == 0);
                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(file_change.update_reason == update_type_t::content);
                }
            }
            SECTION("updates mediator") {
                auto own_name = bfs::path(L"файл.bin");
                auto sub_path = root_path / own_name;
                write_file(sub_path, "12345");
                updates_mediator->push(narrow(sub_path.generic_wstring()), deadline);

                target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                poll();
                REQUIRE(changes.size() == 0);

                target->push(deadline, folder_id, narrow(own_name.wstring()), U::created);
                poll();
                REQUIRE(changes.size() == 1);

                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == narrow(own_name.wstring()));
                CHECK(proto::get_size(file_change) == 5);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                CHECK(proto::get_permissions(file_change));
            }
        }
    };
    F().run();
}

void test_start_n_shutdown() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void main() noexcept override {
            launch_target();
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::OPERATIONAL);
            REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

            target->do_shutdown();
            sup->do_process();
            CHECK(static_cast<r::actor_base_t *>(target.get())->access<to::state>() == r::state_t::SHUT_DOWN);
            REQUIRE(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);
        }
    };
    F(false).run();
}

void test_double_watching() {
    struct F : fixture_t {
        using fixture_t::fixture_t;

        void on_watch(message::watch_folder_t &msg) noexcept override {
            auto &counter = msg.payload.ec ? watched_errors : watched_successes;
            ++counter;
            ++watched_replies;
        }

        void main() noexcept override {
            auto folder_id = std::string("my-folder-id");
            auto back_addr = sup->get_address();
            auto ec = utils::make_error_code(utils::error_code_t::no_action);
            sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
            sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
            sup->do_process();
            REQUIRE(watched_replies == 2);
            REQUIRE(watched_successes == 1);
            REQUIRE(watched_errors == 1);
        }

        int watched_successes = 0;
        int watched_errors = 0;
    };
    F().run();
}

void test_real_impl() {
    struct F : fixture_t {
        using fixture_t::fixture_t;
        void main() noexcept override {
            auto folder_id = std::string("my-folder-id");
            auto back_addr = sup->get_address();
            auto ec = utils::make_error_code(utils::error_code_t::no_action);

            SECTION("(create) new dir") {
                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                auto path = root_path / "my-dir";
                bfs::create_directories(path);
                poll();
                REQUIRE(changes.size() == 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == "my-dir");
                CHECK(proto::get_size(file_change) == 0);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::DIRECTORY);
                CHECK(proto::get_permissions(file_change));
                CHECK(file_change.update_reason == update_type_t::created);
            }
            SECTION("(create with recursion) new dir + new file") {
                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                auto path_dir = root_path / "my-dir";
                bfs::create_directories(path_dir);
                poll();
                {
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == "my-dir");
                    CHECK(proto::get_size(file_change) == 0);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::DIRECTORY);
                    CHECK(proto::get_permissions(file_change));
                }

                auto path_file = path_dir / L"файл.bin";
                write_file(path_file, "12345");
                poll();
                {
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(L"my-dir/файл.bin"));
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(file_change.update_reason == update_type_t::created);
                }
            }
            SECTION("(content change) file") {
                auto path = root_path / "my-file";
                write_file(path, "12345");

                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                write_file(path, "123456");

                poll();
                REQUIRE(changes.size() == 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == "my-file");
                CHECK(proto::get_size(file_change) == 6);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                CHECK(proto::get_permissions(file_change));
                CHECK(file_change.update_reason == update_type_t::content);
            }
            SECTION("(delete) single file") {
                auto path = root_path / "my-file";
                write_file(path, "12345");

                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                bfs::remove(path);

                poll();
                REQUIRE(changes.size() == 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == "my-file");
                CHECK(proto::get_size(file_change) == 0);
                CHECK(proto::get_deleted(file_change));
                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                CHECK(file_change.update_reason == update_type_t::deleted);
            }
            SECTION("move") {
                auto subdir_path = root_path / "my-root";
                bfs::create_directories(subdir_path);
                SECTION("outside of my dir => delete") {
                    auto path_1 = subdir_path / "my-file.1";
                    auto path_2 = root_path / "my-file.2";
                    write_file(path_1, "12345");

                    sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, subdir_path, folder_id,
                                                            ec);
                    sup->do_process();
                    REQUIRE(watched_replies == 1);

                    bfs::rename(path_1, path_2);

                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == "my-file.1");
                    CHECK(proto::get_size(file_change) == 0);
                    CHECK(proto::get_deleted(file_change));
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(file_change.update_reason == update_type_t::deleted);
                }
                SECTION("into my dir => create") {
                    auto path_1 = root_path / "my-file.1";
                    auto path_2 = subdir_path / "my-file.2";
                    write_file(path_1, "12345");

                    sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, subdir_path, folder_id,
                                                            ec);
                    sup->do_process();
                    REQUIRE(watched_replies == 1);

                    bfs::rename(path_1, path_2);

                    poll();
                    REQUIRE(changes.size() == 1);
                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == "my-file.2");
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(!proto::get_deleted(file_change));
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(file_change.update_reason == update_type_t::created);
                }
            }
#ifndef SYNCSPIRIT_WIN
            SECTION("(permissions) file") {
                auto path = root_path / "my-file";
                write_file(path, "12345");
                bfs::permissions(path, bfs::perms::owner_read);

                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                bfs::permissions(path, bfs::perms::owner_write);
                poll();
                REQUIRE(changes.size() == 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == "my-file");
                CHECK(proto::get_size(file_change) == 5);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                CHECK(proto::get_permissions(file_change));
                CHECK(file_change.update_reason == update_type_t::meta);
            }
            SECTION("(delete + create) symlink target change") {
                auto path = root_path / "my-file";
                auto link_target_1 = std::string_view("/some/where/1");
                auto link_target_2 = std::string_view("/some/where/2");
                bfs::create_symlink(bfs::path(link_target_1), path);

                sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id, ec);
                sup->do_process();
                REQUIRE(watched_replies == 1);

                bfs::remove(path);
                bfs::create_symlink(bfs::path(link_target_2), path);

                poll();
                REQUIRE(changes.size() == 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == "my-file");
                CHECK(proto::get_size(file_change) == 0);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::SYMLINK);
                CHECK(proto::get_symlink_target(file_change) == link_target_2);
            }
#endif
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_watcher_base, "test_watcher_base", "[fs]");
#if defined(SYNCSPIRIT_WATCHER_ANY)
    REGISTER_TEST_CASE(test_start_n_shutdown, "test_start_n_shutdown", "[fs]");
    REGISTER_TEST_CASE(test_double_watching, "test_double_watching", "[fs]");
    REGISTER_TEST_CASE(test_real_impl, "test_real_impl", "[fs]");
#endif
    return 1;
}

static int v = _init();
