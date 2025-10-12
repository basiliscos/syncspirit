// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "fs/file_actor.h"
#include "fs/utils.h"
#include "net/names.h"
#include "test_supervisor.h"
#include "access.h"
#include "utils/error_code.h"
#include <filesystem>
#include <boost/nowide/convert.hpp>
#include <optional>
#include <utility>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;

namespace bfs = std::filesystem;
using perms_t = std::filesystem::perms;

namespace {

struct fixture_t;

using io_commands_t = fs::message::io_commands_t;
using io_commands_t_ptr_t = r::intrusive_ptr_t<io_commands_t>;

struct chain_builder_t {
    template <typename Reply>
    chain_builder_t(fixture_t *fixture_, io_commands_t_ptr_t msg, std::in_place_type_t<Reply>) : fixture{fixture_} {
        message = msg;
        if (msg) {
            REQUIRE(msg->payload.size() == 1);
            auto reply = std::get_if<Reply>(&msg->payload.front());
            REQUIRE(reply);
            auto &result = reply->result;
            if (result) {
                response = sys::error_code{};
            } else {
                response = result.assume_error();
            }
            message.reset();
        }
    }

    fixture_t &check_success() noexcept {
        REQUIRE(response);
        CHECK(!*response);
        return *fixture;
    }

    fixture_t &check_fail(const sys::error_code &ec = {}) noexcept {
        REQUIRE(response);
        CHECK(*response);
        if (ec) {
            CHECK(*response == ec);
        } else {
            CHECK(response->message() != "");
        }
        return *fixture;
    }

    std::optional<sys::error_code> response;
    io_commands_t_ptr_t message;
    fixture_t *fixture;
};

struct fixture_t {

    fixture_t() noexcept : root_path{unique_path()}, path_guard{root_path} { bfs::create_directory(root_path); }

    virtual configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::db, sup->get_address()); });
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<io_commands_t>([&](io_commands_t &msg) { reply = &msg; }));
            });
        };
    }

    virtual void run() noexcept {
        r::system_context_t ctx;
        sup = ctx.create_supervisor<supervisor_t>()
                  .auto_finish(false)
                  .auto_ack_io(false)
                  .timeout(timeout)
                  .create_registry()
                  .make_presentation(true)
                  .configure_callback(configure())
                  .finish();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        rw_cache.reset(new fs::file_cache_t(2));
        file_actor = sup->create_actor<fs::file_actor_t>().rw_cache(rw_cache).timeout(timeout).finish();
        sup->do_process();
        sequencer = sup->sequencer;

        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        file_addr = file_actor->get_address();

        main();

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main() noexcept {}

    chain_builder_t append_block(const bfs::path &path, utils::bytes_view_t data, std::uint64_t offset,
                                 std::uint64_t file_size) noexcept {
        auto bytes = utils::bytes_t(data.begin(), data.end());

        auto context = fs::payload::extendended_context_prt_t{};
        auto payload = fs::payload::append_block_t(std::move(context), path, std::move(bytes), offset, file_size);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{};
        cmds.emplace_back(std::move(cmd));
        auto msg = r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->put(std::move(msg));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t clone_block(const bfs::path &target, std::uint64_t target_offset, std::uint64_t target_size,
                                const bfs::path &source, std::uint64_t source_offset,
                                std::uint64_t block_size) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};

        auto payload = fs::payload::clone_block_t(std::move(context), target, target_offset, target_size, source,
                                                  source_offset, block_size);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{};
        cmds.emplace_back(std::move(cmd));
        auto msg = r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->put(std::move(msg));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t finish_file(const bfs::path &path, std::uint64_t file_size, std::int64_t modification_s,
                                bfs::path local_path = {}) noexcept {
        if (local_path.empty()) {
            local_path = path;
        }
        auto context = fs::payload::extendended_context_prt_t{};
        auto payload = fs::payload::finish_file_t(std::move(context), path, local_path, file_size, modification_s);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{};
        cmds.emplace_back(std::move(cmd));
        auto msg = r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->put(std::move(msg));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t remote_copy(const bfs::path &path, const proto::FileInfo &meta) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};
        auto type = proto::get_type(meta);
        auto size = proto::get_size(meta);
        auto deleted = proto::get_deleted(meta);
        auto perms = proto::get_permissions(meta);
        auto modificaiton = proto::get_modified_s(meta);
        auto target = std::string(proto::get_symlink_target(meta));

        auto payload = fs::payload::remote_copy_t(std::move(context), path, type, size, perms, modificaiton, target,
                                                  deleted, false);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{};
        cmds.emplace_back(std::move(cmd));
        auto msg = r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->put(std::move(msg));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    r::address_ptr_t file_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    model::sequencer_ptr_t sequencer;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    bfs::path root_path;
    test::path_guard_t path_guard;
    r::system_context_t ctx;
    io_commands_t_ptr_t reply;
    std::string_view folder_id = "1234-5678";
    fs::file_cache_ptr_t rw_cache;
};
} // namespace

void test_remote_copy() {
    struct F : fixture_t {
        void main() noexcept override {

            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            // proto::set_name(pr_fi, "q.txt");
            proto::set_modified_s(pr_fi, modified);
            proto::set_permissions(pr_fi, 0666);

            SECTION("empty regular file") {
                auto path = root_path / L"папка" / L"файл.txt";
                remote_copy(path, pr_fi).check_success();

                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
                REQUIRE(to_unix(bfs::last_write_time(path)) == 1641828421);

#ifndef SYNCSPIRIT_WIN
                auto status = bfs::status(path);
                auto p = status.permissions();
                CHECK((p & perms_t::owner_read) != perms_t::none);
                CHECK((p & perms_t::owner_write) != perms_t::none);
                CHECK((p & perms_t::group_read) != perms_t::none);
                CHECK((p & perms_t::group_write) != perms_t::none);
                CHECK((p & perms_t::others_read) != perms_t::none);
                CHECK((p & perms_t::others_write) != perms_t::none);
#endif
            }
            SECTION("empty regular file in a subdir") {
                auto path = root_path / L"а" / L"б" / L"в" / L"г" / L"д" / L"файл.txt";

                remote_copy(path, pr_fi).check_success();

                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 0);
                REQUIRE(to_unix(bfs::last_write_time(path)) == 1641828421);

#ifndef SYNCSPIRIT_WIN
                auto status = bfs::status(path);
                auto p = status.permissions();
                CHECK((p & perms_t::owner_read) != perms_t::none);
                CHECK((p & perms_t::owner_write) != perms_t::none);
                CHECK((p & perms_t::group_read) != perms_t::none);
                CHECK((p & perms_t::group_write) != perms_t::none);
                CHECK((p & perms_t::others_read) != perms_t::none);
                CHECK((p & perms_t::others_write) != perms_t::none);
#endif
            }
            SECTION("non-empty regular file") {
                proto::set_size(pr_fi, 5);
                auto path = root_path / L"папка" / L"файл.txt";
                remote_copy(path, pr_fi).check_success();

                auto tmp_path = path.parent_path() / (path.filename().wstring() + L".syncspirit-tmp");
                REQUIRE(bfs::exists(tmp_path));
                REQUIRE(bfs::file_size(tmp_path) == 5);

#ifndef SYNCSPIRIT_WIN
                auto status = bfs::status(path);
                auto p = status.permissions();
                CHECK((p & perms_t::owner_read) != perms_t::none);
                CHECK((p & perms_t::owner_write) != perms_t::none);
                CHECK((p & perms_t::group_read) != perms_t::none);
                CHECK((p & perms_t::group_write) != perms_t::none);
                CHECK((p & perms_t::others_read) != perms_t::none);
                CHECK((p & perms_t::others_write) != perms_t::none);
#endif
            }
            SECTION("directory") {
                auto path = root_path / L"папка";
                proto::set_type(pr_fi, proto::FileInfoType::DIRECTORY);
                remote_copy(path, pr_fi).check_success();
                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::is_directory(path));
            }
            SECTION("symlink") {
                SECTION("existing file") {
                    auto path = root_path / L"папка" / L"файл.txt";
                    bfs::path target = root_path / "content";
                    proto::set_type(pr_fi, proto::FileInfoType::SYMLINK);
                    proto::set_symlink_target(pr_fi, boost::nowide::narrow(target.wstring()));

                    write_file(target, "zzz");
                    remote_copy(path, pr_fi).check_success();
#ifndef SYNCSPIRIT_WIN
                    CHECK(bfs::exists(path));
                    CHECK(bfs::is_symlink(path));
                    CHECK(bfs::read_symlink(path) == target);
#endif
                }
                SECTION("non-existing file") {
                    auto path = root_path / L"папка" / L"файл.txt";
                    bfs::path target = root_path / "content";
                    proto::set_type(pr_fi, proto::FileInfoType::SYMLINK);
                    proto::set_symlink_target(pr_fi, boost::nowide::narrow(target.wstring()));

                    remote_copy(path, pr_fi).check_success();

                    CHECK(!bfs::exists(path));
#ifndef SYNCSPIRIT_WIN
                    CHECK(bfs::is_symlink(path));
                    CHECK(bfs::read_symlink(path) == target);
#endif
                }
            }
            SECTION("deleted file") {
                auto name = bfs::path(L"папка") / L"файл.bin";
                pr_fi = {};
                proto::set_name(pr_fi, boost::nowide::narrow(name.generic_wstring()));
                proto::set_modified_s(pr_fi, modified);
                proto::set_deleted(pr_fi, true);

                bfs::path target = root_path / name;
                bfs::create_directories(target.parent_path());
                write_file(target, "zzz");
                REQUIRE(bfs::exists(target));

                remote_copy(target, pr_fi).check_success();

                REQUIRE(!bfs::exists(target));

                remote_copy(target, pr_fi).check_success();
                REQUIRE(!bfs::exists(target));
            }
        }
    };
    F().run();
}

void test_append_block() {
    struct F : fixture_t {
        void main() noexcept override {
            std::int64_t modified = 1641828421;

            auto path_rel = bfs::path(L"путявка") / bfs::path(L"инфо.txt");
            auto path_wstr = path_rel.generic_wstring();
            auto path_str = boost::nowide::narrow(path_wstr);

            auto data_1 = as_owned_bytes("12345");

            SECTION("finish non-opened") {
                auto path = bfs::absolute(root_path / path_rel);
                auto ec = utils::make_error_code(utils::error_code_t::nonunique_filename);
                finish_file(path, 5, 1641828421).check_fail(ec);
            }

            SECTION("file with 1 block") {
                auto path = bfs::absolute(root_path / path_rel);
                append_block(path, data_1, 0, 5).check_success().finish_file(path, 5, 1641828421).check_success();

                REQUIRE(bfs::exists(path));
                REQUIRE(bfs::file_size(path) == 5);
                CHECK(data_1 == as_bytes(read_file(path)));
                CHECK(to_unix(bfs::last_write_time(path)) == 1641828421);
            }
            SECTION("file with 2 different blocks") {
                auto wfilename = boost::nowide::widen(path_str) + L".syncspirit-tmp";
                auto filename = boost::nowide::narrow(wfilename);
                auto tmp_path = root_path / filename;
                auto path = root_path / path_wstr;

                auto data = as_owned_bytes("12345");

                append_block(path, data, 0, 10).check_success();

#ifndef SYNCSPIRIT_WIN
                REQUIRE(bfs::exists(tmp_path));
                REQUIRE(bfs::file_size(tmp_path) == 10);
                CHECK(read_file(tmp_path).substr(0, 5) == "12345");
#endif
                append_block(path, as_owned_bytes("67890"), 5, 10).check_success();

                SECTION("add 2nd block") {
                    finish_file(path, 5, 1641828421).check_success();
                    REQUIRE(!bfs::exists(tmp_path));
                    REQUIRE(bfs::exists(path));
                    REQUIRE(bfs::file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data == "1234567890");
                    CHECK(to_unix(bfs::last_write_time(path)) == 1641828421);
                }

#ifndef SYNCSPIRIT_WIN
                SECTION("remove folder (simulate err)") {
                    bfs::remove_all(root_path);
                    finish_file(path, 5, 1641828421).check_fail();
                }
#endif
            }
        }
    };
    F().run();
}

void test_clone_block() {
    struct F : fixture_t {
        void main() noexcept override {
            std::int64_t modified = 1641828421;

            SECTION("source & target are different files") {
                auto source_path = root_path / L"ать.txt";
                auto target_path = root_path / L"ять.txt";

                SECTION("single block target file") {
                    auto data = as_owned_bytes("12345");
                    append_block(source_path, data, 0, 5)
                        .check_success()
                        .finish_file(source_path, 5, modified)
                        .check_success()
                        .clone_block(target_path, 0, 5, source_path, 0, 5)
                        .check_success()
                        .finish_file(target_path, 5, modified)
                        .check_success();

                    REQUIRE(bfs::exists(target_path));
                    REQUIRE(bfs::file_size(target_path) == 5);
                    CHECK(read_file(target_path) == "12345");
                    CHECK(to_unix(bfs::last_write_time(target_path)) == modified);
                }
                SECTION("multi block target file") {
                    auto data_1 = as_owned_bytes("12345");
                    auto data_2 = as_owned_bytes("67890");
                    append_block(source_path, data_1, 0, 10)
                        .check_success()
                        .append_block(source_path, data_2, 5, 10)
                        .check_success()
                        .finish_file(source_path, 10, modified)
                        .check_success()
                        .clone_block(target_path, 0, 10, source_path, 0, 5)
                        .check_success()
                        .clone_block(target_path, 5, 10, source_path, 5, 5)
                        .check_success()
                        .finish_file(target_path, 10, modified)
                        .check_success();

                    REQUIRE(bfs::exists(target_path));
                    REQUIRE(bfs::file_size(target_path) == 10);
                    CHECK(read_file(target_path) == "1234567890");
                    CHECK(to_unix(bfs::last_write_time(target_path)) == modified);
                }
                SECTION("source/target different sizes") {
                    auto data_1 = as_owned_bytes("12345");
                    auto data_2 = as_owned_bytes("67890");
                    append_block(source_path, data_2, 0, 5)
                        .check_success()
                        .finish_file(source_path, 5, modified)
                        .check_success()
                        .append_block(target_path, data_1, 0, 10)
                        .check_success()
                        .clone_block(target_path, 5, 10, source_path, 0, 5)
                        .check_success()
                        .finish_file(target_path, 10, modified)
                        .check_success();

                    REQUIRE(bfs::exists(target_path));
                    REQUIRE(bfs::file_size(target_path) == 10);
                    CHECK(read_file(target_path) == "1234567890");
                    CHECK(to_unix(bfs::last_write_time(target_path)) == modified);
                }
            }
            SECTION("source & target are is the same file") {
                auto target_path = root_path / L"ы" / L"ять.txt";
                auto data = as_owned_bytes("12345");
                append_block(target_path, data, 0, 10)
                    .check_success()
                    .clone_block(target_path, 5, 10, target_path, 0, 5)
                    .check_success()
                    .finish_file(target_path, 10, modified)
                    .check_success();

                REQUIRE(bfs::exists(target_path));
                REQUIRE(bfs::file_size(target_path) == 10);
                CHECK(read_file(target_path) == "1234512345");
                CHECK(to_unix(bfs::last_write_time(target_path)) == modified);
            }
        }
    };
    F().run();
}

void test_requesting_block() {
    struct F : fixture_t {
        void main() noexcept override {
            bfs::path target = root_path / "a.txt";

            std::int64_t modified = 1641828421;

            auto fs_addr = file_actor->get_address();
            auto back_addr = sup->get_address();

            auto context = fs::payload::extendended_context_prt_t{};

            auto payload = fs::payload::block_request_t(std::move(context), target, 0, 5);
            auto cmd = fs::payload::io_command_t(std::move(payload));
            auto cmds = fs::payload::io_commands_t{};
            cmds.emplace_back(std::move(cmd));
            auto msg =
                r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));

            SECTION("error, no file") {
                sup->put(std::move(msg));
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(reply->payload.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&reply->payload.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_error());
            }

            SECTION("error, oversized request") {
                write_file(target, "1234");
                sup->put(std::move(msg));
                sup->do_process();
                REQUIRE(reply);
                REQUIRE(reply->payload.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&reply->payload.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_error());
            }

            SECTION("successful file reading") {
                write_file(target, "1234567890");
                sup->put(std::move(msg));
                sup->do_process();

                REQUIRE(reply);
                REQUIRE(reply->payload.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&reply->payload.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_value());
                REQUIRE(reply_payload->result.value() == as_bytes("12345"));

                reply.reset();

                auto context = fs::payload::extendended_context_prt_t{};
                auto payload = fs::payload::block_request_t(std::move(context), target, 5, 5);
                auto cmd = fs::payload::io_command_t(std::move(payload));
                auto cmds = fs::payload::io_commands_t{};
                cmds.emplace_back(std::move(cmd));
                auto msg =
                    r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
                sup->put(std::move(msg));
                sup->do_process();

                REQUIRE(reply);
                REQUIRE(reply->payload.size() == 1);
                reply_payload = std::get_if<decltype(payload)>(&reply->payload.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_value());
                REQUIRE(reply_payload->result.value() == as_bytes("67890"));
            }
        }
    };
    F().run();
}

int _init() {
    test::init_logging();
    REGISTER_TEST_CASE(test_remote_copy, "test_remote_copy", "[fs]");
    REGISTER_TEST_CASE(test_append_block, "test_append_block", "[fs]");
    REGISTER_TEST_CASE(test_clone_block, "test_clone_block", "[fs]");
    REGISTER_TEST_CASE(test_requesting_block, "test_requesting_block", "[fs]");
    return 1;
}

static int v = _init();
