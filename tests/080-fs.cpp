#include "catch.hpp"
#include "test-utils.h"
#include "test_supervisor.h"
#include "access.h"
#include "db/utils.h"
#include "fs/utils.h"
#include "fs/fs_actor.h"
#include "utils/error_code.h"
#include <ostream>
#include <fstream>
#include <stdio.h>
#include <net/names.h>

namespace st = syncspirit::test;
namespace fs = syncspirit::fs;

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using namespace syncspirit::model;

std::string static hash_string(const std::string_view &hash) noexcept {
    auto r = std::string();
    r.reserve(hash.size() * 2);
    for (size_t i = 0; i < hash.size(); ++i) {
        char buff[3];
        sprintf(buff, "%02x", (unsigned char)hash[i]);
        r += std::string_view(buff, 2);
    }
    return r;
}

static void write(const bfs::path &path, const std::string_view &data) {
    std::ofstream f(path.c_str());
    f.write(data.data(), data.size());
}

TEST_CASE("utils", "[fs]") {
    auto dir = bfs::current_path() / bfs::unique_path();
    bfs::create_directory(dir);
    auto dir_guard = st::path_guard_t(dir);
    auto sample_file = dir / bfs::unique_path();

    SECTION("file does not exists") {
        auto opt = prepare(sample_file);
        CHECK(!opt);
    }

    SECTION("empty file") {
        std::ofstream out(sample_file.c_str(), std::ios::app);
        auto opt = prepare(sample_file);
        CHECK(opt);
        CHECK(!opt.value());
    }

    SECTION("3 bytes file") {
        std::ofstream out(sample_file.c_str(), std::ios::app);
        out.write("hi\n", 3);
        out.close();
        auto opt = prepare(sample_file);
        CHECK(opt);
        CHECK(opt.value());
        auto &b = opt.value().value();
        CHECK(b.block_index == 0);
        CHECK(b.block_size == 3);
        CHECK(b.file_size == 3);
        CHECK(b.path == sample_file);

        SECTION("digest") {
            auto block = compute(b);
            REQUIRE(block);
            CHECK(block->get_size() == 3);
            CHECK(hash_string(block->get_hash()) == "98ea6e4f216f2fb4b69fff9b3a44842c38686ca685f3f55dc48c5d3fb1107be4");
            CHECK(block->get_weak_hash() == 0x21700dc);
        }
    }

    SECTION("400kb bytes file") {
        std::ofstream out(sample_file.c_str(), std::ios::app);
        std::string data;
        data.resize(400 * 1024);
        for (size_t i = 0; i < data.size(); ++i) {
            data[i] = 1;
        }
        out.write(data.data(), data.size());
        out.close();
        auto opt = prepare(sample_file);
        CHECK(opt);
        CHECK(opt.value());
        auto &b = opt.value().value();
        CHECK(b.block_index == 0);
        CHECK(b.block_size == 128 * 1024);
        CHECK(b.file_size == data.size());
        CHECK(b.path == sample_file);

        SECTION("digest") {
            auto b1 = compute(b);
            REQUIRE(b1);
            CHECK(b1->get_size() == 128 * 1024);
            CHECK(hash_string(b1->get_hash()) == "4017b7a27f5d49ed213ab864b83f7d1f706ecc1039001dadcffed8df6bccddd1");
            CHECK(b1->get_weak_hash() == 0x1ef001f);

            b.block_index++;
            auto b2 = compute(b);
            CHECK(*b2 == *b1);

            b.block_index++;
            auto b3 = compute(b);
            CHECK(*b3 == *b1);

            b.block_index++;
            auto b4 = compute(b);
            CHECK(!(*b4 == *b1));
            CHECK(b4->get_size() == (400 - 128 * 3) * 1024);
        }
    }

    SECTION("relative") {
        CHECK(fs::relative(bfs::path("a/b/c"), bfs::path("a")).path == bfs::path("b/c"));
        CHECK(fs::relative(bfs::path("a/b/c.syncspirit-tmp"), bfs::path("a")).path == bfs::path("b/c"));
    }
}

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
            [&](auto &p) { p.discover_name(net::names::fs, fs_actor, true).link(); });

        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&scan_consumer_t::on_response);
            p.subscribe_actor(&scan_consumer_t::on_error);
        });
    }

    void on_start() noexcept {
        r::actor_base_t::on_start();
        void *ptr = (void *)&scan_consumer_t::some_function;
        send<payload::scan_request_t>(fs_actor, root_path, get_address(), 0UL, ptr);
    }

    static void some_function() noexcept {}

    void on_response(message::scan_response_t &msg) noexcept {
        response = &msg;
        get_supervisor().do_shutdown();
    }

    void on_error(message::scan_error_t &msg) noexcept { errors.emplace_back(&msg); }
};

struct write_consumer_t : r::actor_base_t {
    using r::actor_base_t::actor_base_t;
    using res_ptr_t = r::intrusive_ptr_t<message::write_response_t>;

    r::address_ptr_t fs_actor;
    res_ptr_t response;

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>(
            [&](auto &p) { p.discover_name(net::names::fs, fs_actor, true).link(); });

        plugin.with_casted<r::plugin::starter_plugin_t>(
            [&](auto &p) { p.subscribe_actor(&write_consumer_t::on_response); });
    }

    void on_response(message::write_response_t &res) noexcept { response = &res; }

    void make_request(bfs::path path, const std::string &data, const std::string &hash, bool final) noexcept {
        request<payload::write_request_t>(fs_actor, path, data, hash, final).send(init_timeout);
    }
};

TEST_CASE("fs-actor", "[fs]") {
    auto root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = path_guard_t(root_path);
    r::system_context_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<st::supervisor_t>().timeout(timeout).create_registry().finish();
    sup->start();

    SECTION("scan") {
        sup->create_actor<fs_actor_t>().fs_config({1024, 5, 0}).timeout(timeout).finish();
        SECTION("empty path") {
            auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
            sup->do_process();
            CHECK(act->errors.empty());
            REQUIRE(act->response);
            auto &r = act->response->payload.map_info;
            CHECK(r->root == root_path);
            CHECK(r->map.empty());

            // custom payload is preserved
            void *ptr_orig = (void *)&scan_consumer_t::some_function;
            CHECK(act->response->payload.custom_payload == ptr_orig);
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
            write(file, "hi\n");
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
            write(file, data);
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
    };

    SECTION("write") {
        sup->create_actor<fs_actor_t>().fs_config({1024, 5, 0}).timeout(timeout).finish();
        auto act = sup->create_actor<write_consumer_t>().timeout(timeout).finish();
        sup->do_process();

        const std::string data = "123456980";
        std::string hash;
        hash.resize(SHA256_DIGEST_LENGTH);
        utils::digest(data.data(), data.size(), hash.data());

        SECTION("success case") {
            auto path = root_path / "my-file";

            act->make_request(path, data, hash, false);
            sup->do_process();

            REQUIRE(act->response);
            auto &r = act->response->payload;
            CHECK(!r.ee);
            CHECK(read_file(root_path / "my-file.syncspirit-tmp") == data);

            SECTION("tmp file is missing") {
                auto act = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
                sup->do_process();
                CHECK(act->errors.empty());
                REQUIRE(act->response);
                auto &r = act->response->payload.map_info;
                CHECK(r->root == root_path);
                CHECK(r->map.empty());
            }

            SECTION("append/final") {
                act->response.reset();
                const std::string tail = "abcdefg";
                utils::digest(tail.data(), tail.size(), hash.data());
                act->make_request(path, tail, hash, true);
                sup->do_process();
                REQUIRE(act->response);
                auto &r = act->response->payload;
                CHECK(!r.ee);
                CHECK(read_file(path) == data + tail);
            }
        }

        SECTION("success case in subfolder") {
            auto path = root_path / "dir" / "my-file";
            act->make_request(path, data, hash, false);
            sup->do_process();

            REQUIRE(act->response);
            auto &r = act->response->payload;
            CHECK(!r.ee);
            CHECK(read_file(root_path / "dir" / "my-file.syncspirit-tmp") == data);
        }

        SECTION("wrong hash") {
            auto path = root_path / "my-file-with-wrong-hash";
            hash[0] = hash[0] * -1;
            act->make_request(path, data, hash, false);
            sup->do_process();

            REQUIRE(act->response);
            auto &r = act->response->payload;
            CHECK(!bfs::exists(path));
            CHECK(r.ee);
            auto ec = r.ee->ec;
            CHECK(ec.value() == (int)utils::protocol_error_code_t::digest_mismatch);
        }
    }

    SECTION("scan temporaries") {
        sup->create_actor<fs_actor_t>().fs_config({1024, 5, 10}).timeout(timeout).finish();
        auto writer = sup->create_actor<write_consumer_t>().timeout(timeout).finish();
        sup->do_process();

        auto path = root_path / "my-file";
        const std::string data = "123456980";
        std::string hash;
        hash.resize(SHA256_DIGEST_LENGTH);
        utils::digest(data.data(), data.size(), hash.data());

        writer->make_request(path, data, hash, false);
        sup->do_process();
        REQUIRE(writer->response);
        CHECK(!writer->response->payload.ee);
        CHECK(read_file(root_path / "my-file.syncspirit-tmp") == data);

        auto scaner = sup->create_actor<scan_consumer_t>().timeout(timeout).root_path(root_path).finish();
        sup->do_process();
        CHECK(scaner->errors.empty());
        REQUIRE(scaner->response);
        auto &r = scaner->response->payload.map_info;
        CHECK(r->root == root_path);
        CHECK(!r->map.empty());
        auto it = r->map.begin();
        auto &file = it->second;
        CHECK(file.temp);
        CHECK(it->first == "my-file");
    }
    sup->shutdown();
    sup->do_process();
}
