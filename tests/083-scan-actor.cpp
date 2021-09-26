#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "fs/utils.h"
#include "fs/scan_actor.h"
#include "hasher/hasher_actor.h"
#include "hasher/hasher_proxy_actor.h"
#include "net/names.h"
#include "utils/error_code.h"
#include <net/names.h>

namespace st = syncspirit::test;
namespace fs = syncspirit::fs;

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using namespace syncspirit::model;

struct consumer_actor_config_t : r::actor_config_t {
    bfs::path root_path;
};

template <typename Actor> struct consumer_actor_config_builder_t : r::actor_config_builder_t<Actor> {
    using builder_t = typename Actor::template config_builder_t<Actor>;
    using parent_t = r::actor_config_builder_t<Actor>;
    using parent_t::parent_t;

    builder_t &&root_path(const bfs::path &value) &&noexcept {
        parent_t::config.root_path = value;
        return std::move(*static_cast<typename parent_t::builder_t *>(this));
    }
};

struct scan_consumer_t : r::actor_base_t {
    using config_t = consumer_actor_config_t;
    using res_ptr_t = r::intrusive_ptr_t<message::scan_response_t>;
    using err_ptr_t = r::intrusive_ptr_t<message::scan_error_t>;
    using errors_t = std::vector<err_ptr_t>;
    template <typename Actor> using config_builder_t = consumer_actor_config_builder_t<Actor>;

    bfs::path root_path;
    r::address_ptr_t fs_actor;
    res_ptr_t response;
    errors_t errors;

    explicit scan_consumer_t(config_t &cfg) : r::actor_base_t{cfg}, root_path{cfg.root_path} {}

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name(net::names::scan_actor, fs_actor, true).link(); });

        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&scan_consumer_t::on_response);
            p.subscribe_actor(&scan_consumer_t::on_error);
        });
    }

    void on_start() noexcept {
        r::actor_base_t::on_start();
        send<payload::scan_request_t>(fs_actor, root_path, get_address(), 0UL);
    }

    void on_response(message::scan_response_t &msg) noexcept {
        response = &msg;
        get_supervisor().do_shutdown();
    }

    void on_error(message::scan_error_t &msg) noexcept { errors.emplace_back(&msg); }
};

TEST_CASE("scan-actor", "[fs]") {
    utils::set_default("trace");

    auto root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = path_guard_t(root_path);
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->create_actor<hasher::hasher_actor_t>().index(1).timeout(timeout).finish();
    auto hasher = sup->create_actor<hasher::hasher_proxy_actor_t>()
                      .hasher_threads(1)
                      .name(net::names::hasher_proxy)
                      .timeout(timeout)
                      .finish()
                      ->get_address();
    sup->start();
    sup->create_actor<scan_actor_t>()
        .fs_config({1024, 5})
        .hasher_proxy(hasher)
        .requested_hashes_limit(2)
        .timeout(timeout)
        .finish();
    sup->do_process();

    SECTION("empty path") {
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        CHECK(r->root == root_path);
        CHECK(r->map.empty());
    }

    SECTION("non-existing root path") {
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path / "bla-bla").finish();
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        CHECK(r->root == (root_path / "bla-bla"));
        CHECK(r->map.empty());
    }

    SECTION("path with dummy file, in root folder and in subfolder") {
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        auto sub = GENERATE(as<std::string>{}, "", "a/b/");
        auto dir = root_path / sub;
        bfs::create_directories(dir);
        auto file = dir / "my-file";
        write_file(file, "hi\n");
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        REQUIRE(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first.string() == bfs::path(sub) / "my-file");
        auto &local = it->second;
        auto &blocks = local.blocks;
        REQUIRE(blocks.size() == 1);
        auto &b = blocks.front();
        CHECK(hash_string(b->get_hash()) == "98ea6e4f216f2fb4b69fff9b3a44842c38686ca685f3f55dc48c5d3fb1107be4");
        CHECK(b->get_weak_hash() == 0x21700dc);
    }

    SECTION("file with 2 identical blocks") {
        static const constexpr size_t SZ = (1 << 7) * 1024;
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        auto file = root_path / "my-file";
        std::string data;
        data.resize(SZ * 2);
        std::fill(data.begin(), data.end(), 1);
        write_file(file, data);
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        REQUIRE(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first == "my-file");
        auto &local = it->second;
        auto &blocks = local.blocks;
        REQUIRE(blocks.size() == 2);
        auto &b1 = blocks[0];
        auto &b2 = blocks[1];
        CHECK(*b1 == *b2);
        CHECK(b1 == b2);
    }

    SECTION("file with 2 different blocks") {
        static const constexpr size_t SZ = (1 << 7) * 1024;
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        auto file = root_path / "my-file";
        std::string data;
        data.resize(SZ * 2);
        std::fill(data.begin(), data.begin() + SZ, 1);
        std::fill(data.begin() + SZ, data.begin() + SZ * 2, 2);
        write_file(file, data);
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        REQUIRE(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first == "my-file");
        auto &local = it->second;
        auto &blocks = local.blocks;
        REQUIRE(blocks.size() == 2);
        auto &b1 = blocks[0];
        auto &b2 = blocks[1];
        CHECK(*b1 != *b2);
        CHECK(b1 != b2);
        CHECK(hash_string(b1->get_hash()) == "4017b7a27f5d49ed213ab864b83f7d1f706ecc1039001dadcffed8df6bccddd1");
        CHECK(hash_string(b2->get_hash()) == "964e0165a36948b60ea2e603b1641612b64713783b789275dc51b4f260d5e0a1");
    }

    SECTION("symlink") {
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        auto file = root_path / "my-file";
        bfs::create_symlink("to-some-where", file);
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        REQUIRE(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first == "my-file");
        auto &local = it->second;
        CHECK(local.blocks.size() == 0);
        CHECK(local.file_type == model::local_file_t::symlink);
        CHECK(local.symlink_target == "to-some-where");
    }

    SECTION("dir") {
        auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        auto dir = root_path / "my-dir";
        bfs::create_directory(dir);
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto &r = act->response->payload.map_info;
        REQUIRE(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first == "my-dir");
        auto &local = it->second;
        CHECK(local.blocks.size() == 0);
        CHECK(local.file_type == model::local_file_t::dir);
    }

    SECTION("scan temporaries") {

        auto path = root_path / "my-file";
        const std::string data = "123456980";
        std::string hash;
        hash.resize(SHA256_DIGEST_LENGTH);
        utils::digest(data.data(), data.size(), hash.data());

        write_file(root_path / "my-file.syncspirit-tmp", data);

        auto scaner = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        sup->do_process();
        CHECK(scaner->errors.empty());
        REQUIRE(scaner->response);
        auto &r = scaner->response->payload.map_info;
        CHECK(r->root == root_path);
        CHECK(!r->map.empty());
        auto it = r->map.begin();
        CHECK(it->first == path.filename());
        auto &file = it->second;
        CHECK(file.temp);
        CHECK(it->first == "my-file");
    }

    sup->shutdown();
    sup->do_process();
}
