#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "db/utils.h"
#include "fs/utils.h"
#include "fs/fs_actor.h"
#include <ostream>
#include <fstream>
#include <stdio.h>
#include <rotor/thread.hpp>
#include <net/names.h>


namespace rth = rotor::thread;

using namespace syncspirit;
using namespace syncspirit::fs;
using namespace syncspirit::test;
using namespace syncspirit::model;


std::string static hash_string(const std::string_view& hash) noexcept {
    auto r = std::string();
    r.reserve(hash.size() * 2);
    for (size_t i = 0; i < hash.size(); ++i) {
        char buff[3];
        sprintf(buff, "%02x", (unsigned char)hash[i]);
        r += std::string_view(buff, 2);
    }
    return r;
}

static void write(const bfs::path& path, const std::string_view& data) {
    std::ofstream f(path.c_str());
    f.write(data.data(), data.size());
}

bool operator==(const block_info_t& l, const block_info_t& r) noexcept {
    return l.get_hash() == r.get_hash() && l.get_size() == r.get_size() && l.get_weak_hash() == r.get_weak_hash();
}

TEST_CASE("utils", "[fs]") {
    auto sample_file = bfs::unique_path();
    auto sample_file_guard = path_guard_t(sample_file);

    SECTION("file does not exists") {
        auto opt = prepare(sample_file);
        CHECK(!opt);
        CHECK(opt.error() == sys::errc::no_such_file_or_directory);
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
        auto& b = opt.value().value();
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
        for(size_t i = 0; i < data.size(); ++i) {
            data[i] = 1;
        }
        out.write(data.data(), data.size());
        out.close();
        auto opt = prepare(sample_file);
        CHECK(opt);
        CHECK(opt.value());
        auto& b = opt.value().value();
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


struct consumer_actor_t: r::actor_base_t {
    using config_t = consumer_actor_config_t;
    using res_ptr_t = r::intrusive_ptr_t<message::scan_response_t>;
    using err_ptr_t = r::intrusive_ptr_t<message::scan_error_t>;
    using errors_t = std::vector<err_ptr_t>;
    template <typename Actor> using config_builder_t = consumer_actor_config_builder_t<Actor>;

    bfs::path root_path;
    r::address_ptr_t fs_actor;
    res_ptr_t response;
    errors_t errors;

    explicit consumer_actor_t(config_t &cfg): r::actor_base_t{cfg}, root_path{cfg.root_path} {
    }

    void configure(r::plugin::plugin_base_t &plugin) noexcept {
        r::actor_base_t::configure(plugin);
        plugin.with_casted<r::plugin::registry_plugin_t>([&](auto &p) {
            p.discover_name(net::names::fs, fs_actor, true).link();
        });

        plugin.with_casted<r::plugin::starter_plugin_t>([&](auto &p) {
            p.subscribe_actor(&consumer_actor_t::on_response);
            p.subscribe_actor(&consumer_actor_t::on_error);
        });
    }

    void on_start() noexcept {
        r::actor_base_t::on_start();
        send<payload::scan_request_t>(fs_actor, root_path, get_address());
    }

    void on_response(message::scan_response_t& msg) noexcept {
        response = &msg;
        get_supervisor().do_shutdown();
    }

    void on_error(message::scan_error_t& msg) noexcept {
        errors.emplace_back(&msg);
    }
};


TEST_CASE("fs-actor", "[fs]") {
    auto root_path = bfs::unique_path();
    bfs::create_directory(root_path);
    auto root_path_guard = path_guard_t(root_path);
    rth::system_context_thread_t ctx;
    auto timeout = r::pt::milliseconds{10};
    auto sup = ctx.create_supervisor<rth::supervisor_thread_t>().timeout(timeout).create_registry().finish();
    sup->create_actor<fs_actor_t>().fs_config({1024, 5}).timeout(timeout).finish();
    sup->start();

    SECTION("empty path") {
        auto act = sup->create_actor<consumer_actor_t>().timeout(timeout).root_path(root_path).finish();
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto& r = act->response->payload;
        CHECK(r.root == root_path);
        CHECK(r.file_map.empty());
    }

    SECTION("non-existing root path") {
        auto act = sup->create_actor<consumer_actor_t>().timeout(timeout).root_path(root_path / "bla-bla").finish();
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto& r = act->response->payload;
        CHECK(r.root == (root_path / "bla-bla"));
        CHECK(r.file_map.empty());
    }

    SECTION("path with dummy file, in root folder and in subfolder") {
        auto act = sup->create_actor<consumer_actor_t>().timeout(timeout).root_path(root_path).finish();
        auto sub = GENERATE(as<std::string>{}, "", "a/b/");
        auto dir = root_path / sub;
        bfs::create_directories(dir);
        auto file = dir / "my-file";
        write(file, "hi\n");
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto& r = act->response->payload;
        REQUIRE(!r.file_map.empty());
        auto it = r.file_map.begin();
        CHECK(it->first == file);
        auto& blocks = it->second;
        REQUIRE(blocks.size() == 1);
        auto& b = blocks.front();
        CHECK(hash_string(b->get_hash()) == "98ea6e4f216f2fb4b69fff9b3a44842c38686ca685f3f55dc48c5d3fb1107be4");
        CHECK(b->get_weak_hash() == 0x21700dc);
    }

    SECTION("file with 2 identical blocks") {
        static const constexpr size_t SZ = (1 << 7) * 1024;
        auto act = sup->create_actor<consumer_actor_t>().timeout(timeout).root_path(root_path).finish();
        auto file = root_path / "my-file";
        std::string data;
        data.resize(SZ*2);
        std::fill(data.begin(), data.end(), 1);
        write(file, data);
        sup->do_process();
        CHECK(act->errors.empty());
        REQUIRE(act->response);
        auto& r = act->response->payload;
        REQUIRE(!r.file_map.empty());
        auto it = r.file_map.begin();
        CHECK(it->first == file);
        auto& blocks = it->second;
        REQUIRE(blocks.size() == 2);
        auto& b1 = blocks[0];
        auto& b2 = blocks[1];
        CHECK(*b1 == *b2);
        CHECK(b1 == b2);
    }

    sup->shutdown();
    sup->do_process();
}
