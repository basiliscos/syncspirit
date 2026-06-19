// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "fs/file_actor.h"
#include "fs/utils.h"
#include "fs/platform/context_base.h"
#include "net/names.h"
#include "test_supervisor.h"
#include "access.h"
#include "utils/error_code.h"
#include "syncspirit-config.h"
#include <optional>
#include <utility>
#include <utils/platform.h>

using namespace syncspirit;
using namespace syncspirit::db;
using namespace syncspirit::test;
using namespace syncspirit::model;
using namespace syncspirit::net;
using namespace syncspirit::fs;

namespace {

struct fixture_t;

using io_commands_t = fs::message::io_commands_t;
using io_commands_t_ptr_t = r::intrusive_ptr_t<io_commands_t>;

struct my_context_t final : platform::context_base_t {
    using parent_t = platform::context_base_t;
    my_context_t() : parent_t(pt::milliseconds{1}) {};
    void poll_events() noexcept override {};
};

struct chain_builder_t {
    template <typename Reply>
    chain_builder_t(fixture_t *fixture_, io_commands_t_ptr_t msg, std::in_place_type_t<Reply>) : fixture{fixture_} {
        message = msg;
        if (msg) {
            auto &commands = msg->payload.commands;
            REQUIRE(commands.size() == 1);
            auto reply = std::get_if<Reply>(&commands.front());
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

    fixture_t() noexcept : path_guard{unique_path()} {}

    virtual configure_callback_t configure() noexcept {
        return [&](r::plugin::plugin_base_t &plugin) {
            plugin.template with_casted<r::plugin::registry_plugin_t>(
                [&](auto &p) { p.register_name(net::names::db, sup->get_address()); });
            plugin.template with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
                p.subscribe_actor(r::lambda<io_commands_t>([&](io_commands_t &msg) { reply = &msg; }));
            });
        };
    }

    virtual void create_file_actor() noexcept {
        file_actor = sup->create_actor<fs::file_actor_t>()
                         .timeout(timeout)
                         .change_retension(retension)
                         .updates_mediator(updates_mediator)
                         .watched_folders(watched_folders)
                         .finish();
    }

    virtual void run() noexcept {
        auto ctx = my_context_t();
        sup = ctx.create_supervisor<supervisor_t>()
                  .auto_finish(false)
                  .auto_ack_io(false)
                  .timeout(timeout)
                  .create_registry()
                  .configure_callback(configure())
                  .finish();

        sup->start();
        sup->do_process();
        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::OPERATIONAL);

        updates_mediator = new fs::updates_mediator_t(retension);
        watched_folders.reset(new watched_folders_t());

        watched_folders->emplace(std::make_pair(folder_id, path_guard.clone()));

        create_file_actor();
        sup->do_process();
        sequencer = sup->sequencer;

        CHECK(static_cast<r::actor_base_t *>(file_actor.get())->access<to::state>() == r::state_t::OPERATIONAL);
        file_addr = file_actor->get_address();

        auto buffer = std::array<std::byte, 1024 * 32>();
        auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
        auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

        main(path_guard.get_view(allocator));

        sup->shutdown();
        sup->do_process();

        CHECK(static_cast<r::actor_base_t *>(sup.get())->access<to::state>() == r::state_t::SHUT_DOWN);
    }

    virtual void main(const utils::poly_path_view_t&) noexcept {}

    chain_builder_t append_block(const utils::poly_path_view_t &path, utils::bytes_view_t data, std::uint64_t offset,
                                 std::uint64_t file_size) noexcept {
        auto bytes = utils::bytes_t(data.begin(), data.end());

        auto context = fs::payload::extendended_context_prt_t{};
        auto payload =
            fs::payload::append_block_t(std::move(context), folder_id, path.detach(), std::move(bytes), offset, file_size);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t clone_block(const utils::poly_path_view_t &target, std::uint64_t target_offset, std::uint64_t target_size,
                                const utils::poly_path_view_t &source, std::uint64_t source_offset,
                                std::uint64_t block_size) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};

        auto payload = fs::payload::clone_block_t(std::move(context), folder_id, target.detach(), target_offset, target_size,
                                                  source.detach(), source_offset, block_size);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t finish_file(const utils::poly_path_view_t &path, std::uint64_t file_size, std::int64_t modification_s,
                                std::uint32_t permissions, bool no_permissions,
                                const utils::poly_path_view_t &conflict_path) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};
        auto payload = fs::payload::finish_file_t(std::move(context), folder_id, path.detach(), conflict_path.detach(), file_size,
                                                  modification_s, permissions, no_permissions);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t remote_copy(const utils::poly_path_view_t &path, const proto::FileInfo &meta,
                                const utils::poly_path_view_t &conflict_path) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};
        auto type = proto::get_type(meta);
        auto size = proto::get_size(meta);
        auto deleted = proto::get_deleted(meta);
        auto perms = proto::get_permissions(meta);
        auto modificaiton = proto::get_modified_s(meta);
        auto target = std::string(proto::get_symlink_target(meta));

        auto payload = fs::payload::remote_copy_t(std::move(context), folder_id, path.detach(), conflict_path.detach(), type, size, perms,
                                                  modificaiton, target, deleted, false);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    chain_builder_t update_meta(const utils::poly_path_view_t &path, std::int64_t modification_s_, std::uint32_t permissions_,
                                bool no_permissions_) noexcept {
        auto context = fs::payload::extendended_context_prt_t{};

        auto payload = fs::payload::update_meta_t(std::move(context), folder_id, path.detach(), modification_s_, permissions_,
                                                  no_permissions_);
        auto cmd = fs::payload::io_command_t(std::move(payload));
        auto cmds = fs::payload::io_commands_t{nullptr};
        cmds.commands.emplace_back(std::move(cmd));
        sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));
        sup->do_process();
        return chain_builder_t(this, reply, std::in_place_type_t<decltype(payload)>());
    }

    r::address_ptr_t file_addr;
    r::pt::time_duration timeout = r::pt::millisec{10};
    r::pt::time_duration retension = r::pt::microseconds{1};
    model::sequencer_ptr_t sequencer;
    r::intrusive_ptr_t<supervisor_t> sup;
    r::intrusive_ptr_t<fs::file_actor_t> file_actor;
    fs::updates_mediator_ptr_t updates_mediator;
    fs::watched_folders_ptr_t watched_folders;
    test::path_guard_t path_guard;
    r::system_context_t ctx;
    io_commands_t_ptr_t reply;
    std::string folder_id = "1234-5678";
};
} // namespace

void test_remote_copy() {
    struct F : fixture_t {
        void main(const utils::poly_path_view_t& root_path) noexcept override {
            proto::FileInfo pr_fi;
            std::int64_t modified = 1641828421;
            proto::set_modified_s(pr_fi, modified);
            proto::set_permissions(pr_fi, 0666);
            auto empty_path = utils::make_empty_view(root_path.get_allocator());

            SECTION("empty regular file") {
                auto path = root_path / L"папка" / L"файл.txt";
                remote_copy(path, pr_fi, empty_path).check_success();

                REQUIRE(exists(path));
                REQUIRE(file_size(path) == 0);
                REQUIRE(last_write_time(path) == 1641828421);
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);

#ifndef SYNCSPIRIT_WIN
                CHECK((permissions(path) & 0666));
#endif
            }
            SECTION("empty regular file in a subdir") {
                auto path = root_path / L"а" / L"б" / L"в" / L"г" / L"д" / L"файл.txt";

                remote_copy(path, pr_fi, empty_path).check_success();

                REQUIRE(exists(path));
                REQUIRE(file_size(path) == 0);
                REQUIRE(last_write_time(path) == 1641828421);
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);

#ifndef SYNCSPIRIT_WIN
                CHECK((permissions(path) & 0666));
#endif
            }
            SECTION("non-empty regular file") {
                proto::set_size(pr_fi, 5);
                auto path = root_path / L"папка" / L"файл.txt";
                write_file(path, "12345");
                remote_copy(path, pr_fi, empty_path).check_success();

                auto filename = L"файл.txt.syncspirit-tmp";
                auto tmp_path = path.get_parent() / utils::make_native_view(filename, path.get_allocator());
                REQUIRE(!exists(tmp_path));

                CHECK(last_write_time(path) == 1641828421);
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
#ifndef SYNCSPIRIT_WIN
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);
                CHECK((permissions(path) & 0666));
#else
                CHECK(updates_mediator->is_masked(path.get_full_name()) == 1);
#endif
            }
            SECTION("directory") {
                auto path = root_path / L"папка";
                proto::set_type(pr_fi, proto::FileInfoType::DIRECTORY);
                remote_copy(path, pr_fi, empty_path).check_success();
                REQUIRE(exists(path));
                REQUIRE(is_directory(path));
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 1);
            }
            SECTION("symlink") {
                SECTION("existing file") {
                    auto path = root_path / L"папка" / L"файл.txt";
                    auto target = root_path / "content";
                    proto::set_type(pr_fi, proto::FileInfoType::SYMLINK);
                    proto::set_symlink_target(pr_fi, target.get_full_name());

                    write_file(target, "123");
                    remote_copy(path, pr_fi, empty_path).check_success();
#ifndef SYNCSPIRIT_WIN
                    CHECK(updates_mediator->is_masked(path.get_full_name()) == 1);
                    CHECK(exists(path));
                    CHECK(is_symlink(path));
                    CHECK(read_symlink(path).data() == target.get_full_name());
#endif
                }
                SECTION("non-existing file") {
                    auto path = root_path / L"папка" / L"файл.txt";
                    auto target = root_path / "content";
                    proto::set_type(pr_fi, proto::FileInfoType::SYMLINK);
                    proto::set_symlink_target(pr_fi, target.get_full_name());

                    remote_copy(path, pr_fi, empty_path).check_success();

                    CHECK(!exists(target));
#ifndef SYNCSPIRIT_WIN
                    CHECK(exists(path));
                    CHECK(updates_mediator->is_masked(path.get_full_name()) == 1);
                    CHECK(is_symlink(path));
                    CHECK(read_symlink(path).data() == target.get_full_name());
#else
                    CHECK(!exists(path));
#endif
                }
            }
            SECTION("deleted file") {
                auto name = utils::make_native_view(L"папка/файл.bin", root_path.get_allocator());
                pr_fi = {};
                proto::set_name(pr_fi, name.get_full_name());
                proto::set_modified_s(pr_fi, modified);
                proto::set_deleted(pr_fi, true);

                auto target = root_path / name;
                create_directories(target.get_parent());
                write_file(target, "zzz");
                REQUIRE(exists(target));

                remote_copy(target, pr_fi, empty_path).check_success();
                REQUIRE(!exists(target));
#ifndef SYNCSPIRIT_WATCHER_KQUEUE
                CHECK(updates_mediator->is_masked(target.get_full_name()) == 1);
#else
                CHECK(updates_mediator->is_masked(target.get_parent().get_full_name()) == 1);
#endif
                remote_copy(target, pr_fi, empty_path).check_success();
                CHECK(updates_mediator->is_masked(target.get_full_name()) == 0);
                REQUIRE(!exists(target));
            }
            SECTION("conflict") {
                auto name = utils::make_native_view(L"папка/файл.bin", root_path.get_allocator());
                proto::set_name(pr_fi, name.get_full_name());
                proto::set_modified_s(pr_fi, modified);

                auto target = root_path / name;
                auto conflict = target.get_parent() / L"конфликт.bin";
                create_directories(target.get_parent());
                write_file(target, "123");
                REQUIRE(exists(target));

                remote_copy(target, pr_fi, conflict).check_success();
                CHECK(exists(target));
                CHECK(exists(conflict));
                CHECK(as_owned_bytes("123") == as_bytes(read_file(conflict)));

                CHECK(updates_mediator->is_masked(target.get_full_name()) >= 3);
                CHECK(updates_mediator->is_masked(conflict.get_full_name()) == 1);
            }
        }
    };
    F().run();
}

void test_append_block() {
    struct F : fixture_t {
        void main(const utils::poly_path_view_t& root_path) noexcept override {
            std::int64_t modified = 1641828421;

            auto path_rel = utils::make_native_view(L"путявка/инфо.txt", root_path.get_allocator());

            auto data_1 = as_owned_bytes("12345");
            auto perms = std::uint32_t(0444);
            auto no_perms = !utils::platform_t::permissions_supported(path_rel);
            auto empty_path = utils::make_empty_view(root_path.get_allocator());
            SECTION("attempt finish non-existing") {
                auto path = root_path / path_rel;
                auto ec = utils::make_error_code(utils::error_code_t::flush_non_opened);
                finish_file(path, 5, 1641828421, perms, no_perms, empty_path).check_fail(ec);
                CHECK(updates_mediator->is_masked(path.get_full_name()) == 0);
            }
            SECTION("finish unflushed") {
                auto dir_path = root_path / path_rel.get_parent();
                create_directories(dir_path);
                auto tmp_path = dir_path / L"инфо.txt.syncspirit-tmp";
                write_file(tmp_path, "12345");
                auto path = root_path / path_rel;
                finish_file(path, 5, 1641828421, perms, no_perms, empty_path).check_success();
                REQUIRE(exists(path));
                REQUIRE(file_size(path) == 5);
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
                CHECK(data_1 == as_bytes(read_file(path)));
                CHECK(last_write_time(path) == 1641828421);
                if (!no_perms) {
                    CHECK(permissions(path) == perms);
                }
            }
            SECTION("file with 1 block") {
                auto path = root_path / path_rel;
                auto tmp_path = path.make_temporal();
                append_block(path, data_1, 0, 5)
                    .check_success()
                    .finish_file(path, 5, 1641828421, perms, no_perms, empty_path)
                    .check_success();

                REQUIRE(exists(path));
                REQUIRE(file_size(path) == 5);
                CHECK(data_1 == as_bytes(read_file(path)));
                CHECK(last_write_time(path) == 1641828421);
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
                CHECK(updates_mediator->is_masked(path.get_parent().get_full_name()) == 2);
#endif
                if (!no_perms) {
                    CHECK(permissions(path) == perms);
                }
            }
            SECTION("file with 1 block & conflict rename") {
                auto path = root_path / path_rel;
                auto tmp_path = path.make_temporal();
                write_file(path, "abcdef");
                auto conflict_path = path.get_parent() / L"экс-инфо.txt";
                append_block(path, data_1, 0, 5)
                    .check_success()
                    .finish_file(path, 5, 1641828421, perms, no_perms, conflict_path)
                    .check_success();

                REQUIRE(exists(path));
                CHECK(file_size(path) == 5);
                CHECK(data_1 == as_bytes(read_file(path)));
                CHECK(last_write_time(path) == 1641828421);
                if (!no_perms) {
                    CHECK(permissions(path) == perms);
                }

                REQUIRE(exists(conflict_path));
                CHECK(file_size(conflict_path) == 6);
                CHECK(as_bytes(read_file(conflict_path)) == as_owned_bytes("abcdef"));
                CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
                CHECK(updates_mediator->is_masked(path.get_parent().get_full_name()) == 3);
#endif
            }
            SECTION("file with 2 different blocks") {
                auto path = root_path / path_rel.get_filename();
                auto tmp_path = path.make_temporal();
                auto data = as_owned_bytes("12345");

                append_block(path, data, 0, 10).check_success();

#ifndef SYNCSPIRIT_WIN
                REQUIRE(exists(tmp_path));
                REQUIRE(file_size(tmp_path) == 10);
#endif
                append_block(path, as_owned_bytes("67890"), 5, 10).check_success();
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);

                SECTION("add 2nd block") {
                    finish_file(path, 5, 1641828421, perms, no_perms, empty_path).check_success();
                    REQUIRE(!exists(tmp_path));
                    REQUIRE(exists(path));
                    REQUIRE(file_size(path) == 10);
                    auto data = read_file(path);
                    CHECK(data == "1234567890");
                    CHECK(last_write_time(path) == 1641828421);
                    CHECK(updates_mediator->is_masked(path.get_full_name()) >= 2);
                    if (!no_perms) {
                        CHECK(permissions(path) == perms);
                    }
                }

#ifndef SYNCSPIRIT_WIN
                SECTION("remove folder (simulate err)") {
                    remove_all(root_path);
                    finish_file(path, 5, 1641828421, perms, no_perms, empty_path).check_fail();
                    CHECK(updates_mediator->is_masked(path.get_full_name()) == 0);
                }
#endif
            }
        }
    };
    F().run();
}


void test_clone_block() {
    struct F : fixture_t {
        void main(const utils::poly_path_view_t& root_path) noexcept override {
            std::int64_t modified = 1641828421;
            auto perms = std::uint32_t(0444);
#ifndef SYNCSPIRIT_WIN
            auto no_perms = false;
#else
            auto no_perms = true;
#endif
            auto empty_path = utils::make_empty_view(root_path.get_allocator());
            SECTION("source & target are different files") {
                auto source_path = root_path / L"ать.txt";
                auto target_path = root_path / L"ять.txt";
                auto tmp_path = target_path.make_temporal();

                SECTION("single block target file") {
                    auto data = as_owned_bytes("12345");
                    append_block(source_path, data, 0, 5)
                        .check_success()
                        .finish_file(source_path, 5, modified, perms, no_perms, empty_path)
                        .check_success()
                        .clone_block(target_path, 0, 5, source_path, 0, 5)
                        .check_success()
                        .finish_file(target_path, 5, modified, perms, no_perms, empty_path)
                        .check_success();

                    REQUIRE(exists(target_path));
                    REQUIRE(file_size(target_path) == 5);
                    CHECK(read_file(target_path) == "12345");
                    CHECK(last_write_time(target_path) == modified);
                    CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
                    CHECK(updates_mediator->is_masked(target_path.get_parent().get_full_name()) == 4);
#endif
                }
                SECTION("multi block target file") {
                    auto data_1 = as_owned_bytes("12345");
                    auto data_2 = as_owned_bytes("67890");
                    append_block(source_path, data_1, 0, 10)
                        .check_success()
                        .append_block(source_path, data_2, 5, 10)
                        .check_success()
                        .finish_file(source_path, 10, modified, perms, no_perms, empty_path)
                        .check_success()
                        .clone_block(target_path, 0, 10, source_path, 0, 5)
                        .check_success()
                        .clone_block(target_path, 5, 10, source_path, 5, 5)
                        .check_success()
                        .finish_file(target_path, 10, modified, perms, no_perms, empty_path)
                        .check_success();

                    REQUIRE(exists(target_path));
                    REQUIRE(file_size(target_path) == 10);
                    CHECK(read_file(target_path) == "1234567890");
                    CHECK(last_write_time(target_path) == modified);
                    CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
#ifdef SYNCSPIRIT_WATCHER_KQUEUE
                    CHECK(updates_mediator->is_masked(target_path.get_parent().get_full_name()) == 4);
#endif
                }
                SECTION("source/target different sizes") {
                    auto data_1 = as_owned_bytes("12345");
                    auto data_2 = as_owned_bytes("67890");
                    append_block(source_path, data_2, 0, 5)
                        .check_success()
                        .finish_file(source_path, 5, modified, perms, no_perms, empty_path)
                        .check_success()
                        .append_block(target_path, data_1, 0, 10)
                        .check_success()
                        .clone_block(target_path, 5, 10, source_path, 0, 5)
                        .check_success()
                        .finish_file(target_path, 10, modified, perms, no_perms, empty_path)
                        .check_success();

                    REQUIRE(exists(target_path));
                    REQUIRE(file_size(target_path) == 10);
                    CHECK(read_file(target_path) == "1234567890");
                    CHECK(last_write_time(target_path) == modified);
                    CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
                }
            }
            SECTION("source & target are is the same file") {
                auto target_path = root_path / L"ы" / L"ять.txt";
                auto data = as_owned_bytes("12345");
                append_block(target_path, data, 0, 10)
                    .check_success()
                    .clone_block(target_path, 5, 10, target_path, 0, 5)
                    .check_success()
                    .finish_file(target_path, 10, modified, perms, no_perms, empty_path)
                    .check_success();

                REQUIRE(exists(target_path));
                REQUIRE(file_size(target_path) == 10);
                CHECK(read_file(target_path) == "1234512345");
                CHECK(last_write_time(target_path) == modified);
                auto tmp_path = target_path.make_temporal();
                CHECK(updates_mediator->is_masked(tmp_path.get_full_name()) == 0);
            }
        }
    };
    F().run();
}

void test_update_meta() {
    struct F : fixture_t {
        void main(const utils::poly_path_view_t& root_path) noexcept override {
            std::int64_t modified = 1641828421;
            auto perms = std::uint32_t(0444);
#ifndef SYNCSPIRIT_WIN
            auto no_perms = false;
#else
            auto no_perms = true;
#endif
            auto path = root_path / L"файл.bin";
            auto path_str = path.get_full_name();

            SECTION("file") {
                write_file(path, "12345");
                update_meta(path, modified, perms, no_perms).check_success();
                CHECK(last_write_time(path) == modified);
#ifndef SYNCSPIRIT_WIN
                CHECK(permissions(path) == perms);
#endif
            }
            SECTION("file does not exists") { update_meta(path, modified, perms, no_perms).check_fail(); }

#ifndef SYNCSPIRIT_WIN
            SECTION("dir") {
                create_directories(path);
                update_meta(path, modified, perms, no_perms).check_success();
                CHECK(last_write_time(path) == modified);
                CHECK(permissions(path) == perms);
            }
#endif
        }
    };
    F().run();
}

void test_requesting_block() {
    struct F : fixture_t {
        void main(const utils::poly_path_view_t& root_path) noexcept override {
            auto target = root_path / "a.txt";

            std::int64_t modified = 1641828421;

            auto fs_addr = file_actor->get_address();
            auto back_addr = sup->get_address();

            auto context = fs::payload::extendended_context_prt_t{};

            auto payload = fs::payload::block_request_t(std::move(context), target.detach(), 0, 5);
            auto cmd = fs::payload::io_command_t(std::move(payload));
            auto cmds = fs::payload::io_commands_t{nullptr};
            cmds.commands.emplace_back(std::move(cmd));
            sup->route<fs::payload::io_commands_t>(file_addr, sup->get_address(), std::move(cmds));

            SECTION("error, no file") {
                sup->do_process();
                REQUIRE(reply);
                auto &cmds = reply->payload.commands;
                REQUIRE(cmds.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&cmds.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_error());
            }

            SECTION("error, oversized request") {
                write_file(target, "1234");
                sup->do_process();
                REQUIRE(reply);
                auto &cmds = reply->payload.commands;
                REQUIRE(cmds.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&cmds.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_error());
            }

            SECTION("successful file reading") {
                write_file(target, "1234567890");
                sup->do_process();

                REQUIRE(reply);
                auto &cmds = reply->payload.commands;
                REQUIRE(cmds.size() == 1);
                auto reply_payload = std::get_if<decltype(payload)>(&cmds.front());
                REQUIRE(reply_payload);
                REQUIRE(reply_payload->result.has_value());
                REQUIRE(reply_payload->result.value() == as_bytes("12345"));

                reply.reset();

                auto context = fs::payload::extendended_context_prt_t{};
                auto payload = fs::payload::block_request_t(std::move(context), target.detach(), 5, 5);
                auto cmd = fs::payload::io_command_t(std::move(payload));
                auto command = fs::payload::io_commands_t{};
                command.commands.emplace_back(std::move(cmd));
                auto msg = r::make_routed_message<fs::payload::io_commands_t>(file_addr, sup->get_address(),
                                                                              std::move(command));
                sup->put(std::move(msg));
                sup->do_process();

                REQUIRE(reply);
                REQUIRE(reply->payload.commands.size() == 1);
                reply_payload = std::get_if<decltype(payload)>(&reply->payload.commands.front());
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
    REGISTER_TEST_CASE(test_update_meta, "test_update_meta", "[fs]");
    REGISTER_TEST_CASE(test_requesting_block, "test_requesting_block", "[fs]");
    return 1;
}

static int v = _init();
