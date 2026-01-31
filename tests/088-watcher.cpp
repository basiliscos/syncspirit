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
#include <stdexcept>

using namespace syncspirit;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::fs;
namespace bfs = std::filesystem;
using boost::nowide::narrow;

struct fixture_t;

namespace native {

bool wine_environment() {
#ifdef SYNCSPIRIT_WIN
    if (auto handle = GetModuleHandle("ntdll.dll")) {
        if (GetProcAddress(handle, "wine_get_version")) {
            return true;
        }
    }
#endif
    return false;
}

void rename(const bfs::path &from, const bfs::path &to) {
#ifndef SYNCSPIRIT_WIN
    bfs::rename(from, to);
#else
    auto from_native = from.native().data();
    auto to_native = to.native().data();
    if (!MoveFileExW(from_native, to_native, MOVEFILE_WRITE_THROUGH)) {
        auto ec = sys::error_code(::GetLastError(), sys::system_category());
        throw std::runtime_error(ec.message());
    }
#endif
}

} // namespace native

static const auto RETENSION_TIMEOUT = r::pt::millisec{1};
static const auto TIMEOUT = r::pt::millisec{10};

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

enum class poll_t { single, trigger_timer };

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
        fs_context.reset(new fs::fs_context_t(timeout * 2));
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

    void await_events(poll_t poll_type, size_t await_changes = 0, bool flatten = false) {
        using clock_t = pt::microsec_clock;
        changes.clear();
        auto deadline = clock_t::local_time() + pt::seconds{5};
        do {
            auto prev_sz = changes.size();
            auto has_events = fs_context->wait_next_event();
            fs_context->update_time();
            sup->do_process();
            auto wait_next = (changes.size() < await_changes) || (!has_events && poll_type == poll_t::trigger_timer);
            if (wait_next) {
                fs_context->wait_next_event();
                fs_context->update_time();
                sup->do_process();
            }
            if (flatten) {
                auto copy = change_messages_t();
                for (auto &m : changes) {
                    for (auto &folder_change : m->payload) {
                        auto file_changes = folder_change.file_changes;
                        auto comparator = [](const auto &l, const auto &r) -> bool {
                            return proto::get_name(l) < proto::get_name(r);
                        };
                        std::sort(file_changes.begin(), file_changes.end(), comparator);
                        for (auto &file_change : file_changes) {
                            auto solo_changes = payload::folder_change_t{folder_change.folder_id, {file_change}};
                            auto msg = change_message_ptr_t();
                            msg.reset(new fs::message::folder_changes_t(sup->get_address(), std::move(solo_changes)));
                            copy.emplace_back(msg);
                        }
                    }
                }
                changes = std::move(copy);
            }
        } while ((changes.size() < await_changes) && clock_t::local_time() < deadline);
    }

    virtual void on_watch(message::watch_folder_t &msg) noexcept {
        CHECK(!msg.payload.ec);
        CHECK(msg.payload.ec.message() != "");
        ++watched_replies;
    }

    virtual void on_changes(message::folder_changes_t &msg) noexcept { changes.emplace_back(&msg); }

    virtual void main() noexcept {}

    r::pt::time_duration retension() { return RETENSION_TIMEOUT; }

    bool auto_launch;
    bfs::path root_path;
    test::path_guard_t path_guard;
    fs_context_ptr_r fs_context;
    r::intrusive_ptr_t<supervisor_t> sup;
    fs::updates_mediator_ptr_t updates_mediator;
    target_ptr_t target;
    r::pt::time_duration timeout = TIMEOUT;
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
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                    await_events(poll_t::single, 1);
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
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                    await_events(poll_t::single, 1);
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
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                    await_events(poll_t::single, 1);
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
            SECTION("file moving") {
                auto name_1 = bfs::path(L"файл-1.bin");
                auto name_2 = bfs::path(L"файл-2.bin");
                auto sub_path_1 = root_path / name_1;
                auto sub_path_2 = root_path / name_2;
                auto path_2_str = narrow(sub_path_2.generic_wstring());
                write_file(sub_path_1, "12345");
                target->push(deadline, folder_id, narrow(name_1.wstring()), path_2_str, U::meta);
                await_events(poll_t::single, 1);
                auto &payload = changes.front()->payload;
                REQUIRE(payload.size() == 1);
                auto &folder_change = payload[0];
                REQUIRE(folder_change.folder_id == folder_id);
                REQUIRE(folder_change.file_changes.size() == 1);
                auto &file_change = folder_change.file_changes.front();
                CHECK(proto::get_name(file_change) == narrow(name_1.wstring()));
                CHECK(proto::get_size(file_change) == 5);
                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                CHECK(proto::get_permissions(file_change));
                CHECK(file_change.prev_path == path_2_str);
                CHECK(file_change.update_reason == update_type_t::meta);
            }
            SECTION("changes accumulation") {
                auto deadline_2 = deadline + retension();
                SECTION("simple case") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), {}, U::content);

                    await_events(poll_t::single);
                    REQUIRE(changes.size() == 0);

                    await_events(poll_t::trigger_timer, 1);

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
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), {}, U::deleted);
                    await_events(poll_t::trigger_timer);
                    REQUIRE(changes.size() == 0);
                }
                SECTION("content change in dir -> collapse to void") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    bfs::create_directory(sub_path);
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::content);
                    await_events(poll_t::trigger_timer);
                    REQUIRE(changes.size() == 0);
                }
                SECTION("content , meta -> collapse to content") {
                    auto own_name = bfs::path(L"файл.bin");
                    auto sub_path = root_path / own_name;
                    write_file(sub_path, "12345");
                    target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::content);
                    target->push(deadline_2, folder_id, narrow(own_name.wstring()), {}, U::meta);

                    await_events(poll_t::trigger_timer, 1);

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
                SECTION("move, content, meta -> collapse to content, preserve original name") {
                    auto name_1 = bfs::path(L"файл-1.bin");
                    auto name_2 = bfs::path(L"файл-2.bin");
                    auto name_1_str = narrow(name_1.generic_wstring());
                    auto name_2_str = narrow(name_2.generic_wstring());
                    write_file(root_path / name_2, "12345");
                    target->push(deadline, folder_id, name_2_str, name_1_str, U::meta);
                    target->push(deadline_2, folder_id, name_2_str, {}, U::content);
                    target->push(deadline_2, folder_id, name_2_str, {}, U::meta);

                    await_events(poll_t::trigger_timer, 1);

                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == narrow(name_2.wstring()));
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(file_change.update_reason == update_type_t::content);
                    CHECK(file_change.prev_path == name_1_str);
                }
                SECTION("move, delete -> collapse to delete of original") {
                    auto name_1 = bfs::path(L"файл-1.bin");
                    auto name_2 = bfs::path(L"файл-2.bin");
                    auto name_1_str = narrow(name_1.generic_wstring());
                    auto name_2_str = narrow(name_2.generic_wstring());
                    target->push(deadline, folder_id, name_2_str, name_1_str, U::meta);
                    target->push(deadline_2, folder_id, name_2_str, {}, U::deleted);

                    await_events(poll_t::trigger_timer, 1);

                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == name_1_str);
                    CHECK(proto::get_size(file_change) == 0);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_deleted(file_change));
                    CHECK(file_change.update_reason == update_type_t::deleted);
                    CHECK(file_change.prev_path.empty());
                }
                SECTION("mv(a, b), mv(b, c) -> collapse to mv(a, c") {
                    auto name_1 = bfs::path(L"файл-1.bin");
                    auto name_2 = bfs::path(L"файл-2.bin");
                    auto name_3 = bfs::path(L"файл-3.bin");
                    auto name_1_str = narrow(name_1.generic_wstring());
                    auto name_2_str = narrow(name_2.generic_wstring());
                    auto name_3_str = narrow(name_3.generic_wstring());
                    write_file(root_path / name_3, "12345");
                    target->push(deadline, folder_id, name_2_str, name_1_str, U::meta);
                    target->push(deadline_2, folder_id, name_3_str, name_2_str, U::meta);

                    await_events(poll_t::trigger_timer);

                    auto &payload = changes.front()->payload;
                    REQUIRE(payload.size() == 1);
                    auto &folder_change = payload[0];
                    REQUIRE(folder_change.folder_id == folder_id);
                    REQUIRE(folder_change.file_changes.size() == 1);
                    auto &file_change = folder_change.file_changes.front();
                    CHECK(proto::get_name(file_change) == name_3_str);
                    CHECK(proto::get_size(file_change) == 5);
                    CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                    CHECK(proto::get_permissions(file_change));
                    CHECK(file_change.update_reason == update_type_t::meta);
                    CHECK(file_change.prev_path == name_1_str);
                }
            }
            SECTION("updates mediator") {
                auto own_name = bfs::path(L"файл.bin");
                auto sub_path = root_path / own_name;
                write_file(sub_path, "12345");
                updates_mediator->push(narrow(sub_path.generic_wstring()), {}, deadline);

                target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                await_events(poll_t::single);
                REQUIRE(changes.size() == 0);

                target->push(deadline, folder_id, narrow(own_name.wstring()), {}, U::created);
                await_events(poll_t::single);
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
                await_events(poll_t::trigger_timer, 1);

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
                await_events(poll_t::trigger_timer, 1);
                {
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
                    changes.clear();
                }

                auto path_file = path_dir / L"файл.bin";
                write_file(path_file, "12345");
                await_events(poll_t::trigger_timer, 1);
                {
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

                await_events(poll_t::trigger_timer, 1);
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

                await_events(poll_t::trigger_timer, 1);
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
                auto a_path = subdir_path / L"a/b/п1";
                auto x_path = subdir_path / L"x/y/п2";
                bfs::create_directories(a_path);
                bfs::create_directories(x_path);
                SECTION("file inside root => meta") {
                    if (!native::wine_environment()) {
                        auto path_1 = subdir_path / L"my-file.1";
                        auto path_2 = subdir_path / L"my-file.2";
                        write_file(path_1, "12345");

                        sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id,
                                                                ec);
                        sup->do_process();
                        REQUIRE(watched_replies == 1);

                        native::rename(path_1, path_2);

#ifndef SYNCSPIRIT_WIN
                        await_events(poll_t::trigger_timer, 1);

                        auto &payload = changes.front()->payload;
                        REQUIRE(payload.size() == 1);
                        auto &folder_change = payload[0];
                        REQUIRE(folder_change.folder_id == folder_id);
                        REQUIRE(folder_change.file_changes.size() == 1);
                        auto &file_change = folder_change.file_changes.front();
                        CHECK(proto::get_name(file_change) == narrow(L"my-root/my-file.2"));
                        CHECK(proto::get_size(file_change) == 5);
                        CHECK(!proto::get_deleted(file_change));
                        CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                        CHECK(file_change.update_reason == update_type_t::meta);
                        CHECK(file_change.prev_path == narrow(L"my-root/my-file.1"));
#else
                        await_events(poll_t::trigger_timer, 2, true);
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            auto &file_change = folder_change.file_changes[0];
                            CHECK(proto::get_name(file_change) == narrow(L"my-root/my-file.1"));
                            CHECK(proto::get_size(file_change) == 0);
                            CHECK(proto::get_deleted(file_change));
                            CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                            CHECK(file_change.update_reason == update_type_t::deleted);
                            CHECK(file_change.prev_path == "");
                            changes.pop_front();
                        }
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            auto &file_change = folder_change.file_changes[0];
                            CHECK(proto::get_name(file_change) == narrow(L"my-root/my-file.2"));
                            CHECK(proto::get_size(file_change) == 5);
                            CHECK(!proto::get_deleted(file_change));
                            CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                            CHECK(file_change.update_reason == update_type_t::created);
                            CHECK(file_change.prev_path == "");
                        }
#endif
                    }
                }
                SECTION("dirs inside folder => meta") {
                    if (!native::wine_environment()) {
                        auto path_1 = subdir_path / L"папка1";
                        auto path_2 = subdir_path / L"папка2";
                        bfs::create_directories(path_1);

                        sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, root_path, folder_id,
                                                                ec);
                        sup->do_process();
                        REQUIRE(watched_replies == 1);

                        native::rename(path_1, path_2);

#ifndef SYNCSPIRIT_WIN
                        await_events(poll_t::trigger_timer, 1);
                        auto &payload = changes.front()->payload;
                        REQUIRE(payload.size() == 1);
                        auto &folder_change = payload[0];
                        REQUIRE(folder_change.folder_id == folder_id);
                        REQUIRE(folder_change.file_changes.size() == 1);
                        auto &file_change = folder_change.file_changes.front();
                        CHECK(proto::get_name(file_change) == narrow(L"my-root/папка2"));
                        CHECK(proto::get_size(file_change) == 0);
                        CHECK(!proto::get_deleted(file_change));
                        CHECK(proto::get_type(file_change) == proto::FileInfoType::DIRECTORY);
                        CHECK(file_change.update_reason == update_type_t::meta);
                        CHECK(file_change.prev_path == narrow(L"my-root/папка1"));
#else
                        await_events(poll_t::trigger_timer, 2, true);
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            auto &file_change = folder_change.file_changes[0];
                            CHECK(proto::get_name(file_change) == narrow(L"my-root/папка1"));
                            CHECK(proto::get_size(file_change) == 0);
                            CHECK(proto::get_deleted(file_change));
                            CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                            CHECK(file_change.update_reason == update_type_t::deleted);
                            CHECK(file_change.prev_path == "");
                            changes.pop_front();
                        }
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            auto &file_change = folder_change.file_changes[0];
                            CHECK(proto::get_name(file_change) == narrow(L"my-root/папка2"));
                            CHECK(proto::get_size(file_change) == 0);
                            CHECK(!proto::get_deleted(file_change));
                            CHECK(proto::get_type(file_change) == proto::FileInfoType::DIRECTORY);
                            CHECK(file_change.update_reason == update_type_t::created);
                            CHECK(file_change.prev_path == "");
                        }
#endif
                    }
                }

                SECTION("outside of my dir => delete") {
                    if (!native::wine_environment()) {
                        auto path_1 = a_path / L"my-file.1";
                        auto path_2 = root_path / L"my-file.2";
                        write_file(path_1, "12345");

                        sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, subdir_path,
                                                                folder_id, ec);
                        sup->do_process();
                        REQUIRE(watched_replies == 1);

                        native::rename(path_1, path_2);

                        await_events(poll_t::trigger_timer, 1);
                        auto &payload = changes.front()->payload;
                        REQUIRE(payload.size() == 1);
                        auto &folder_change = payload[0];
                        REQUIRE(folder_change.folder_id == folder_id);
                        REQUIRE(folder_change.file_changes.size() == 1);
                        auto &file_change = folder_change.file_changes.front();
                        CHECK(proto::get_name(file_change) == narrow(L"a/b/п1/my-file.1"));
                        CHECK(proto::get_size(file_change) == 0);
                        CHECK(proto::get_deleted(file_change));
                        CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                        CHECK(file_change.update_reason == update_type_t::deleted);
                    }
                }
                SECTION("into my dir => create") {
                    if (!native::wine_environment()) {
                        auto path_1 = root_path / L"my-file.1";
                        auto path_2 = x_path / L"my-file.2";
                        write_file(path_1, "12345");

                        sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, subdir_path,
                                                                folder_id, ec);
                        sup->do_process();
                        REQUIRE(watched_replies == 1);

                        native::rename(path_1, path_2);

                        await_events(poll_t::trigger_timer, 1);
                        auto &payload = changes.front()->payload;
                        REQUIRE(payload.size() == 1);
                        auto &folder_change = payload[0];
                        REQUIRE(folder_change.folder_id == folder_id);
                        REQUIRE(folder_change.file_changes.size() == 1);
                        auto &file_change = folder_change.file_changes.front();
                        CHECK(proto::get_name(file_change) == narrow(L"x/y/п2/my-file.2"));
                        CHECK(proto::get_size(file_change) == 5);
                        CHECK(!proto::get_deleted(file_change));
                        CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                        CHECK(file_change.update_reason == update_type_t::created);
                        CHECK(file_change.prev_path.empty());
                    }
                }
                SECTION("inside folder, cross-dir moving => meta") {
                    if (!native::wine_environment()) {
                        auto path_1 = a_path / L"my-file.1";
                        auto path_2 = x_path / L"my-file.2";
                        write_file(path_1, "12345");

                        sup->route<fs::payload::watch_folder_t>(target->get_address(), back_addr, subdir_path,
                                                                folder_id, ec);
                        sup->do_process();
                        REQUIRE(watched_replies == 1);

                        native::rename(path_1, path_2);

#ifndef SYNCSPIRIT_WIN
                        await_events(poll_t::trigger_timer, 1);
                        auto &payload = changes.front()->payload;
                        REQUIRE(payload.size() == 1);
                        auto &folder_change = payload[0];
                        REQUIRE(folder_change.folder_id == folder_id);
                        REQUIRE(folder_change.file_changes.size() == 1);
                        auto &file_change = folder_change.file_changes.front();
                        CHECK(proto::get_name(file_change) == narrow(L"x/y/п2/my-file.2"));
                        CHECK(proto::get_size(file_change) == 5);
                        CHECK(!proto::get_deleted(file_change));
                        CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                        CHECK(file_change.update_reason == update_type_t::meta);
                        CHECK(file_change.prev_path == narrow(L"a/b/п1/my-file.1"));
#else
                        await_events(poll_t::trigger_timer, 2, true);
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            auto &file_change = folder_change.file_changes.front();
                            CHECK(proto::get_name(file_change) == narrow(L"a/b/п1/my-file.1"));
                            CHECK(proto::get_size(file_change) == 0);
                            CHECK(proto::get_deleted(file_change));
                            CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                            CHECK(file_change.update_reason == update_type_t::deleted);
                            CHECK(file_change.prev_path == "");
                            changes.pop_front();
                        }
                        {
                            auto &payload = changes.front()->payload;
                            REQUIRE(payload.size() == 1);
                            auto &folder_change = payload[0];
                            REQUIRE(folder_change.folder_id == folder_id);
                            REQUIRE(folder_change.file_changes.size() == 1);
                            {
                                auto &file_change = folder_change.file_changes[0];
                                CHECK(proto::get_name(file_change) == narrow(L"x/y/п2/my-file.2"));
                                CHECK(proto::get_size(file_change) == 5);
                                CHECK(!proto::get_deleted(file_change));
                                CHECK(proto::get_type(file_change) == proto::FileInfoType::FILE);
                                CHECK(file_change.update_reason == update_type_t::created);
                                CHECK(file_change.prev_path == "");
                            }
                            changes.pop_front();
                        }
#endif
                    }
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
                await_events(poll_t::trigger_timer, 1);
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

                await_events(poll_t::trigger_timer, 1);
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
