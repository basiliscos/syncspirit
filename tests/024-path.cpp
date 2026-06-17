// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025-2026 Ivan Baidakou

#include "test-utils.h"
#include "utils/path.h"
#include "utils/path_view.hpp"
#include "utils/path_cache.h"
#include <memory_resource>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <boost/nowide/convert.hpp>

using namespace syncspirit;
using namespace syncspirit::utils;

using Catch::Matchers::EndsWith;
using Catch::Matchers::StartsWith;

using boost::nowide::narrow;

TEST_CASE("path", "[model]") {
    using pieces_t = std::vector<std::string_view>;
    SECTION("a/bb/c.txt") {
        auto p = path_t::make_generic("a/bb/c.txt");
        SECTION("cloing") {
            auto p2 = path_t::make_generic("a/bb/c.txt");
            CHECK(p == p2);
            auto p4 = std::move(p2);
            CHECK(p4 == p);
            CHECK(p2.empty());
        }

        CHECK(p.get_parent_name() == "a/bb");
        CHECK(p.get_filename() == "c.txt");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 3);
        CHECK(pieces[0] == "a");
        CHECK(pieces[1] == "bb");
        CHECK(pieces[2] == "c.txt");
        CHECK(p.contains(p));
        CHECK(!p.contains(path_t::make_generic("a/bb/c.tx")));
        CHECK(!p.contains(path_t::make_generic("a/bb/c.x")));
        CHECK(!p.contains(path_t::make_generic("a/bb/c")));
        CHECK(!p.contains(path_t::make_generic("a/bb")));
        CHECK(!p.contains(path_t::make_generic("a")));
        CHECK(path_t::make_generic("a").contains(p));
        CHECK(path_t::make_generic("a/").contains(p));
        CHECK(path_t::make_generic("a/b").contains(p));
        CHECK(path_t::make_generic("a/bb/c").contains(p));
    }
    SECTION("dir/file.bin") {
        auto p = path_t::make_generic("dir/file.bin");
        CHECK(p.get_parent_name() == "dir");
        CHECK(p.get_filename() == "file.bin");

        auto pieces = pieces_t();
        for (auto p : p) {
            pieces.emplace_back(p);
        }

        CHECK(pieces.size() == 2);
        CHECK(pieces[0] == "dir");
        CHECK(pieces[1] == "file.bin");
    }
    SECTION("single") {
        auto p = path_t::make_generic("file.bin");
        CHECK(p.get_filename() == "file.bin");
        CHECK(p.get_full_name() == "file.bin");
        CHECK(p.get_parent_name() == "");
    }
    SECTION("root") {
        auto p = path_t::make_generic("/");
        CHECK(p.get_filename() == "");
        CHECK(p.get_full_name() == "/");
        CHECK(p.get_parent_name() == "");
    }
    SECTION("parsing + components iterator") {
        auto str = "/user/home/.config/syncspirit_test/some/path";
        auto p = path_t::make_generic(str);
        CHECK(p.get_components() == 6);
        auto it = p.begin();

        CHECK(*it == "");
        ++it;
        CHECK(*it == "user");
        ++it;
        CHECK(*it == "home");
        ++it;
        CHECK(*it == ".config");
        ++it;
        CHECK(*it == "syncspirit_test");
        ++it;
        CHECK(*it == "some");
        ++it;
        CHECK(*it == "path");
        ++it;

        CHECK(it == p.end());
    }
    SECTION("extension") {
        auto p1 = path_t::make_generic(L"путь/файл.расш");
        auto p2 = path_t::make_generic(L"путь/файл");
        auto p3 = path_t::make_generic(L"/.путь/файл");
        auto p4 = path_t::make_generic(L"путь/файл.р1.р2");
        CHECK(p1.get_extension() == boost::nowide::narrow(L".расш"));
        CHECK(p2.get_extension().empty());
        CHECK(p3.get_extension().empty());
        CHECK(p4.get_extension() == boost::nowide::narrow(L".р2"));
    }
    SECTION("bug with eq") {
        auto p1 = path_t::make_generic(L"/home/b/development/cpp/syncspirit/build.debug-shared/tmp-utfgowerwpsvk/sub-dir/d");
        auto p2 = path_t::make_generic(L"/home/b/development/cpp/syncspirit/build.debug-shared/tmp-utfgowerwpsvk/sub-dir/e");
        CHECK(p1 != p2);
    }
}

TEST_CASE("path view (1)", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

    SECTION("abs path") {
        auto path = path_t::make_generic("/some/dir/file.bin");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "file.bin");
        CHECK(view.get_parent_name() == "/some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "/some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "/some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.get_full_name() == "/");
            CHECK(p3.get_filename() == "");

            CHECK(p3.get_parent().empty());
        }
    }
    SECTION("dir path") {
        auto path = path_t::make_generic("/some/dir/");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "");
        CHECK(view.get_parent_name() == "/some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "/some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "/some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.get_full_name() == "/");
            CHECK(p3.get_filename() == "");

            CHECK(p3.get_parent().empty());
        }
    }
    SECTION("rel path") {
        auto path = path_t::make_generic("some/dir/file.bin");
        auto view = path.get_view(allocator);
        CHECK(path == view);
        CHECK(view.get_filename() == "file.bin");
        CHECK(view.get_parent_name() == "some/dir");
        SECTION("parent") {
            auto p1 = view.get_parent();
            CHECK(p1.get_full_name() == "some/dir");
            CHECK(p1.get_filename() == "dir");

            auto p2 = p1.get_parent();
            CHECK(p2.get_full_name() == "some");
            CHECK(p2.get_filename() == "some");

            auto p3 = p2.get_parent();
            CHECK(p3.empty());
            CHECK(p3.get_full_name() == "");
            CHECK(p3.get_filename() == "");
            CHECK(p3.get_parent().empty());
        }
    }

    SECTION("temporal") {
        auto path = path_t::make_generic("/some/dir/file.bin");
        CHECK(!path.is_temporal());
        auto view = path.get_view(allocator).make_temporal();
        CHECK(view.get_full_name() == "/some/dir/file.bin.syncspirit-tmp");
        CHECK(view.is_temporal());
        CHECK(!view.get_parent().is_temporal());
        CHECK(view.get_parent().get_full_name() == "/some/dir");
    }

    SECTION("absolutness") {
#ifndef SYNCSPIRIT_WIN
        CHECK(path_t::make_generic("/some/dir/file.bin").is_absolute());
        CHECK(!path_t::make_generic("some/dir/file.bin").is_absolute());
#else
        CHECK(path_t::make_native("c:/some/dir/file.bin").is_absolute());
        CHECK(path_t::make_native("c:\\some\\dir\\file.bin").is_absolute());
        CHECK(!path_t::make_native("some/dir/file.bin").is_absolute());
        CHECK(!path_t::make_native("some\\dir\\file.bin").is_absolute());
#endif
    }

    SECTION("wchar/generic") {
        auto path = path_t::make_generic(L"э/ю/Ё");
        CHECK(path.get_filename() == narrow(L"Ё"));
        CHECK(path.get_parent_name() == narrow(L"э/ю"));
    }

    SECTION("native/backslashes") {
        auto path = path_t::make_native(L"э\\ю\\Ё");
#ifndef SYNCSPIRIT_WIN
        CHECK(path.get_full_name() == narrow(L"э\\ю\\Ё"));
        CHECK(path.get_filename() == narrow(L"э\\ю\\Ё"));
#else
        CHECK(path.get_full_name() == narrow(L"э/ю/Ё"));
        CHECK(path.get_filename() == narrow(L"Ё"));
        CHECK(path.get_parent_name() == narrow(L"э/ю"));
#endif
    }
}

TEST_CASE("path view (2)", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 128>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);

#ifndef SYNCSPIRIT_WIN
    auto p_abs_1 = std::string_view("/a/b");
    auto p_abs_2 = std::string_view("/c/d");
#else
    auto p_abs_1 = std::string_view("c:\\a\\b");
    auto p_abs_2 = std::string_view("c:\\c\\d");
#endif

    SECTION("wide string") {
        auto p = path_t::make_native(p_abs_1);
        auto v = p.get_view(allocator);
#ifndef SYNCSPIRIT_WIN
        CHECK(v.get_full_wname(false) == L"/a/b");
        CHECK(v.get_full_wname(true) == L"/a/b");
#else
        CHECK(v.get_full_wname(false) == L"c:/a/b");
        CHECK(v.get_full_wname(true) == L"c:\\a\\b");
#endif
    }
    SECTION("concat") {
        SECTION("2 relatives") {
            auto p1 = path_t::make_generic("a/b").get_view(allocator);
            auto p2 = path_t::make_generic("c/d").get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr.get_components() == 3);
            CHECK(pr.get_full_name() == "a/b/c/d");
            CHECK(!pr.is_absolute());

            auto pr_2 = pr.detach();
            CHECK(pr == pr_2);
        }
        SECTION("2 absolutes") {
            auto p1 = path_t::make_native(p_abs_1).get_view(allocator);
            auto p2 = path_t::make_native(p_abs_2).get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr == p2);
            CHECK(pr.get_full_name() == p2.get_full_name());
            CHECK(pr.is_absolute());
        }
        SECTION("rel + abs") {
            auto p1 = path_t::make_generic("a/b").get_view(allocator);
            auto p2 = path_t::make_native(p_abs_2).get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr == p2);
            CHECK(pr.get_full_name() == p2.get_full_name());
            CHECK(pr.is_absolute());
        }
        SECTION("abs + rel") {
            auto p1 = path_t::make_native(p_abs_1).get_view(allocator);
            auto p2 = path_t::make_generic("c/d").get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr.get_components() == 4);
            CHECK(pr.is_absolute());
            auto full = std::string(pr.get_full_name());
            REQUIRE_THAT(full, StartsWith(std::string(p1.get_full_name())));
            REQUIRE_THAT(full, EndsWith(std::string(p2.get_full_name())));
        }
        SECTION("real-world example") {
            auto p1 = path_t::make_generic("/user/home/.config/syncspirit_test").get_view(allocator);
            auto p2 = path_t::make_generic("some/path").get_view(allocator);
            auto pr = p1 / p2;
            CHECK(pr.get_full_name() == "/user/home/.config/syncspirit_test/some/path");
            CHECK(pr.get_components() == 6);
#ifndef SYNCSPIRIT_WIN
            CHECK(pr.is_absolute());
#endif
            auto pr_2 = pr.detach();
            CHECK(pr == pr_2);

            auto it = pr.begin();

            CHECK(*it == "");
            ++it;
            CHECK(*it == "user");
            ++it;
            CHECK(*it == "home");
            ++it;
            CHECK(*it == ".config");
            ++it;
            CHECK(*it == "syncspirit_test");
            ++it;
            CHECK(*it == "some");
            ++it;
            CHECK(*it == "path");
            ++it;

            CHECK(it == pr.end());
        }
    }
}

TEST_CASE("path view (3)", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 128>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    SECTION("simple") {
        auto view = make_native_view("file.bin", allocator);
        REQUIRE(!view.empty());
        CHECK(view.get_full_name() == "file.bin");
        CHECK(view.get_filename() == "file.bin");
    }
    SECTION("complex") {
        auto view = make_native_view("dir/file.bin", allocator);
        REQUIRE(!view.empty());
        CHECK(view.get_full_name() == "dir/file.bin");
        CHECK(view.get_filename() == "file.bin");
    }
    SECTION("absolute") {
        auto view = make_native_view("/dir/file.bin", allocator);
        REQUIRE(!view.empty());
        CHECK(view.get_full_name() == "/dir/file.bin");
        CHECK(view.get_filename() == "file.bin");
    }
}

TEST_CASE("path view (4)", "[model]") {
    auto buffer = std::array<std::byte, 1024 * 128>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    SECTION("ascii -> utf8") {
        auto view = make_native_view("abc", allocator);
        CHECK(view.get_full_wname() == L"abc");
        CHECK(view.get_full_name() == "abc");
    }
    SECTION("wchar -> utf8 (1)") {
        auto view = make_native_view(L"ёпрст", allocator);
        CHECK(view.get_full_wname() == L"ёпрст");
    }
    SECTION("wchar -> utf8 (2)") {
        auto view = make_native_view(L"э/ю/Ё", allocator);
        CHECK(view.get_full_wname() == L"э/ю/Ё");
    }
    SECTION("wchar -> utf8 (3)") {
        auto view = make_native_view(L"э\\ю\\Ё", allocator);
        CHECK(view.get_full_wname(true) == L"э\\ю\\Ё");
    }
}

TEST_CASE("path_cache", "[model]") {
    auto cache = path_cache_ptr_t(new path_cache_t());
    auto path = cache->get_path("a/b/c");
    REQUIRE(path);
    CHECK(path->use_count() == 1);
    CHECK(cache->map.size() == 1);

    auto path_2 = cache->get_path("a/b/c");
    REQUIRE(path_2);
    CHECK(path_2 == path);
    CHECK(path->use_count() == 2);
    CHECK(cache->map.size() == 1);

    auto path_3 = cache->get_path("x/y/z");
    REQUIRE(path_3);
    CHECK(path_3->use_count() == 1);
    CHECK(cache->map.size() == 2);

    path_3.reset();
    CHECK(cache->map.size() == 1);

    path_2.reset();
    CHECK(cache->map.size() == 1);
    CHECK(path->use_count() == 1);

    path.reset();
    CHECK(cache->map.size() == 0);
}

static bool _init = []() -> bool {
    test::init_logging();
    return true;
}();
