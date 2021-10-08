#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "fs/utils.h"
#include "fs/file_actor.h"
#include "net/names.h"
#include "utils/error_code.h"
#include <net/names.h>

namespace st = syncspirit::test;
namespace fs = syncspirit::fs;

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct file_consumer_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;
    using open_file_ptr_t = r::intrusive_ptr_t<message::open_response_t>;
    using close_file_ptr_t = r::intrusive_ptr_t<message::close_response_t>;
    using clone_file_ptr_t = r::intrusive_ptr_t<message::clone_response_t>;

    r::address_ptr_t fs_actor;
    open_file_ptr_t open_response;
    close_file_ptr_t close_response;
    clone_file_ptr_t clone_response;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name(net::names::file_actor, fs_actor, true).link(); });

        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&file_consumer_t::on_open);
            p.subscribe_actor(&file_consumer_t::on_close);
            p.subscribe_actor(&file_consumer_t::on_clone);
        });
    }

    void on_open(message::open_response_t &res) noexcept { open_response = &res; }
    void on_close(message::close_response_t &res) noexcept { close_response = &res; }
    void on_clone(message::clone_response_t &res) noexcept { clone_response = &res; }

    void open_request(const bfs::path &path, size_t file_size) noexcept {
        request<payload::open_request_t>(fs_actor, path, file_size, nullptr).send(init_timeout);
    }

    void close_request(fs::opened_file_t file, const bfs::path &path) noexcept {
        request<payload::close_request_t>(fs_actor, std::move(file), path).send(init_timeout);
    }
};

TEST_CASE("utils", "[fs]") {
    CHECK(fs::relative(bfs::path("a/b/c"), bfs::path("a")).path == bfs::path("b/c"));
    CHECK(fs::relative(bfs::path("a/b/c.syncspirit-tmp"), bfs::path("a")).path == bfs::path("b/c"));
}

TEST_CASE("fs-actor", "[fs]") {
    utils::set_default("trace");
    auto root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = path_guard_t(root_path);
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();

    SECTION("open/close") {
        auto fs_actor = sup->create_actor<file_actor_t>().timeout(timeout).finish();
        auto act = sup->create_actor<file_consumer_t>().timeout(timeout).finish();
        sup->do_process();

        const std::string data = "123456980";
        auto path = root_path / "my-dir" / "my-file";

        SECTION("size mismatch => content is erased") {
            auto tmp_path = path.parent_path() / "my-file.syncspirit-tmp";
            write_file(tmp_path, "12345");
            act->open_request(path, data.size());
            sup->do_process();

            REQUIRE(act->open_response);
            auto &r = act->open_response->payload;
            CHECK(!r.ee);
            auto &file = r.res->file;
            CHECK(file);

            const char zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
            auto eq = std::equal(zeroes, zeroes + sizeof(zeroes), file->data());
            CHECK(eq);
        }

        act->open_request(path, data.size());

        SECTION("open missing file") {
            sup->do_process();

            REQUIRE(act->open_response);
            auto &r = act->open_response->payload;
            CHECK(!r.ee);
            auto &file = r.res->file;
            CHECK(file);

            std::copy(data.begin(), data.end(), file->data());

            act->close_request(std::move(file), path);
            sup->do_process();

            REQUIRE(act->close_response);
            CHECK(!act->close_response->payload.ee);
            CHECK(read_file(path) == data);
        }

        SECTION("fail to open") {
            bfs::create_directories(path.parent_path() / "my-file.syncspirit-tmp");
            sup->do_process();

            REQUIRE(act->open_response);
            auto &ee = act->open_response->payload.ee;
            CHECK(ee);
            CHECK(ee->root()->ec.value() == sys::errc::io_error);
        }

        SECTION("fail to close") {
            sup->do_process();

            REQUIRE(act->open_response);
            auto &file = act->open_response->payload.res->file;
            std::copy(data.begin(), data.end(), file->data());

            act->close_request(std::move(file), path);
            bfs::create_directories(path);
            sup->do_process();

            REQUIRE(act->close_response);
            auto &ee = act->close_response->payload.ee;
            CHECK(ee);
            CHECK(ee->root()->ec.value());
        }
    }

    SECTION("cloning blocks") {
        sup->create_actor<file_actor_t>().timeout(timeout).finish();
        auto act = sup->create_actor<file_consumer_t>().timeout(timeout).finish();
        sup->do_process();

        auto source = root_path / "my-source";
        auto dest = root_path / "my-dest";
        auto dest_tmp = root_path / "my-dest.syncspirit-tmp";
        write_file(source, "1234567890");

        // to new file
        act->request<payload::clone_request_t>(act->fs_actor, source, dest, 15ul, 5ul, 5ul, 0ul).send(timeout);
        sup->do_process();
        REQUIRE(act->clone_response);
        CHECK(!act->clone_response->payload.ee);
        REQUIRE(bfs::exists(dest_tmp));
        auto data = read_file(dest_tmp);
        REQUIRE(data.length() == 15);
        CHECK(data.substr(0, 5) == "67890");
        auto tail = data.substr(5);
        auto exp = std::string_view("\0\0\0\0\0\0\0\0\0\0", 10);
        CHECK(tail == exp);

        // to already opende file
        auto &target = act->clone_response->payload.res->file;
        act->request<payload::clone_request_t>(act->fs_actor, source, dest, 15ul, 5ul, 0ul, 5ul, std::move(target))
            .send(timeout);
        sup->do_process();
        REQUIRE(act->clone_response);
        CHECK(!act->clone_response->payload.ee);
        act->clone_response.reset();
        data = read_file(dest_tmp);
        REQUIRE(data.length() == 15);
        CHECK(data.substr(0, 10) == "6789012345");
        tail = data.substr(10);
        exp = std::string_view("\0\0\0\0\0", 5);
        CHECK(tail == exp);
    }

    sup->shutdown();
    sup->do_process();
}
