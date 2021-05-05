#include "catch.hpp"
#include "test-utils.h"
#include "access.h"
#include "db/utils.h"
#include "fs/utils.h"
#include <ostream>
#include <stdio.h>

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

bool operator==(const block_info_t& l, const block_info_t& r) noexcept {
    return l.get_hash() == r.get_hash() && l.get_size() == r.get_size() && l.get_weak_hash() == r.get_weak_hash();
}

TEST_CASE("prepare", "[fs]") {
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
