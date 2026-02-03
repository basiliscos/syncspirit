// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/device_id.h"
#include "utils/base32.h"
#include "utils/log-setup.h"
#include <random>
#include <cstdint>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/nowide/convert.hpp>

int main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }

namespace syncspirit::test {

path_guard_t::path_guard_t() {}
path_guard_t::path_guard_t(const bfs::path &path_) : path{path_} {}
path_guard_t::path_guard_t(path_guard_t &&other) : path() { std::swap(path, other.path); }

path_guard_t::~path_guard_t() {
    if (!path.empty()) {
        if (!getenv("SYNCSPIRIT_TEST_KEEP_PATH")) {
            sys::error_code ec;

            if (bfs::exists(path, ec)) {
                bfs::permissions(path, bfs::perms::owner_all, ec);
                if (ec) {
                    printf("error setting permissions : %s: %s\n", path.string().c_str(), ec.message().c_str());
                }
            }

            ec = {};
            bfs::remove_all(path, ec);
            if (ec) {
                printf("error removing %s : %s\n", path.string().c_str(), ec.message().c_str());
            }
        }
    }
}

bfs::path locate_path(const char *test_file) {
    auto path = bfs::path(test_file);
    if (bfs::exists(path)) {
        return path;
    }
    path = bfs::path("../") / path;
    if (bfs::exists(path)) {
        return path;
    }
    std::string err = "path not found: ";
    err += test_file;
    throw std::runtime_error(err);
}

std::string read_file(const bfs::path &path) {
    sys::error_code ec;
    auto copy = path;
    copy.make_preferred();
#ifndef SYNCSPIRIT_WIN
    auto file_path = path.string();
    auto file_path_c = file_path.c_str();
    auto in = fopen(file_path_c, "rb");
#else
    auto file_path = copy.wstring();
    auto file_path_c = file_path.c_str();
    auto in = _wfopen(file_path_c, L"rb");
#endif
    if (!in) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        spdlog::debug("(test/read) can't open '{}': {}", copy.string(), ec.message());
        return "";
    }

    fseek(in, 0L, SEEK_END);
    auto filesize = ftell(in);
    fseek(in, 0L, SEEK_SET);
    std::vector<char> buffer(filesize, 0);
    auto r = fread(buffer.data(), filesize, 1, in);
    assert(r == 1);
    (void)r;
    fclose(in);
    return std::string(buffer.data(), filesize);
}

void write_file(const bfs::path &path_, std::string_view content) {
    bfs::create_directories(path_.parent_path());
    auto copy = path_;
    copy.make_preferred();
#ifndef SYNCSPIRIT_WIN
    auto file_path = copy.string();
    auto file_path_c = file_path.c_str();
    auto out = fopen(file_path_c, "wb");
#else
    auto file_path = copy.wstring();
    auto file_path_c = file_path.c_str();
    auto out = _wfopen(file_path_c, L"wb");
#endif
    if (!out) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "(test/write) can't open " << copy.string() << " : " << ec.message() << "\n";
        std::abort();
    }
    if (content.size()) {
        auto r = fwrite(content.data(), content.size(), 1, out);
        assert(r);
        (void)r;
    }
    fflush(out);
    fclose(out);
}

utils::bytes_t device_id2sha256(std::string_view device_id_) {
    auto device_id = model::device_id_t::from_string(device_id_).value();
    auto sha256 = device_id.get_sha256();
    return {sha256.begin(), sha256.end()};
}

model::device_ptr_t make_device(std::string_view device_id, std::string_view name) {
    auto id = model::device_id_t::from_string(device_id).value();
    return model::device_t::create(id, name).assume_value();
}

std::string hash_string(const std::string_view &hash) noexcept {
    auto r = std::string();
    r.reserve(hash.size() * 2);
    for (size_t i = 0; i < hash.size(); ++i) {
        r += fmt::format("{:02x}", (unsigned char)hash[i]);
    }
    return r;
}

apply_controller_ptr_t make_apply_controller(model::cluster_ptr_t cluster) {
    return new test_apply_controller_t(std::move(cluster));
}

void init_logging() {
    auto [dist_sink, logger] = utils::create_root_logger();
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    dist_sink->add_sink(console_sink);
    logger->set_pattern(utils::log_pattern);
}

static std::random_device rd;
static std::uniform_int_distribution<std::uint64_t> dist;

bfs::path unique_path() {
    auto n = dist(rd);
    auto view = utils::bytes_view_t(reinterpret_cast<const unsigned char *>(&n), sizeof(n));
    auto random_name = utils::base32::encode(view);
    std::transform(random_name.begin(), random_name.end(), random_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto name = std::wstring(L"tmp-") + boost::nowide::widen(random_name);
    return bfs::absolute(bfs::current_path() / bfs::path(name));
}

utils::bytes_view_t as_bytes(std::string_view str) {
    auto ptr = (const unsigned char *)str.data();
    return {ptr, str.size()};
}

utils::bytes_t as_owned_bytes(std::string_view str) {
    auto ptr = (const unsigned char *)str.data();
    return {ptr, ptr + str.size()};
}

bool has_ipv6() noexcept {
    namespace ip = boost::asio::ip;
    namespace sys = boost::system;
    auto ec = sys::error_code();
    ip::make_address_v6("1:2:3::4", ec);
    return !ec;
}

utils::bytes_t make_key(model::block_info_ptr_t block) {
    static constexpr auto SZ = model::block_info_t::digest_length + 1;
    unsigned char key_storage[SZ];
    auto hash = block->get_hash();
    key_storage[0] = db::prefix::block_info;
    std::copy(hash.begin(), hash.end(), key_storage + 1);
    auto key = utils::bytes_t(key_storage, key_storage + SZ);
    return key;
}

} // namespace syncspirit::test
