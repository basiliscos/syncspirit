// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2026 Ivan Baidakou

#include "test-utils.h"
#include "model/cluster.h"
#include "model/device_id.h"
#include "utils/path_view.hpp"
#include "utils/path_utils.h"
#include "utils/base32.h"
#include "utils/format.hpp"
#include "utils/log-setup.h"
#include "utils/io.h"
#include <random>
#include <cstdint>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <boost/nowide/convert.hpp>
#include <catch2/catch_session.hpp>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32)
#else
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#endif

int main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }

namespace syncspirit::test {

path_guard_t::path_guard_t() {}
path_guard_t::path_guard_t(std::wstring path_) : path_t(utils::path_t::make_native(path_)) {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto view = get_view(allocator);
    auto ec = sys::error_code{};
    utils::create_directories(view, ec);
    if (ec) {
        std::cout << fmt::format("cannot create directory: {}: {}\n", view, ec.message());
    }
}

path_guard_t::~path_guard_t() {
    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto view = get_view(allocator);
    if (!view.empty()) {
        auto wname = view.get_full_wname(true);
        if (!getenv("SYNCSPIRIT_TEST_KEEP_PATH")) {
            sys::error_code ec;

            auto path = std::filesystem::path(wname);
            if (std::filesystem::exists(path, ec)) {
                std::filesystem::permissions(path, std::filesystem::perms::owner_all, ec);
                if (ec) {
                    printf("error setting permissions : %s: %s\n", path.string().c_str(), ec.message().c_str());
                }
            }

            ec = {};
            std::filesystem::remove_all(path, ec);
            if (ec) {
                printf("error removing %s : %s\n", path.string().c_str(), ec.message().c_str());
            }
        }
    }
}

static utils::poly_path_view_t cwd(const utils::allocator_t& allocator) {
#ifdef SYNCSPIRIT_WIN
    wchar_t buff[MAX_PATH];
    if (!::GetCurrentDirectoryW(sizeof(buff), buff)) {
        auto ec = std::error_code(::GetLastError(), std::system_category());
        throw std::runtime_error(fmt::format("getcwd: {}", ec.message()));
    }
    return utils::make_native_view(buff, allocator);
#else
    char buff[PATH_MAX];
    if (!getcwd(buff, sizeof(buff))) {
        auto ec = std::error_code(errno, std::system_category());
        throw std::runtime_error(fmt::format("getcwd: {}", ec.message()));
    }
    return utils::make_native_view(buff, allocator);
#endif
}

utils::poly_path_view_t locate_path(const char *test_file, const utils::allocator_t& allocator) {
    auto current = cwd(allocator);
    auto path = current / test_file;
    if (exists(path)) {
        return path;
    }
    path = current.get_parent() / test_file;
    if (exists(path)) {
        return path;
    }
    auto err = fmt::format("path not found: '{}'", path);
    throw std::runtime_error(err);
}

std::string read_file(const utils::poly_path_view_t &path) {
    auto file_opt = utils::io_stream_t::open_read(path);
    if (!file_opt) {
        spdlog::debug("(test/read) can't open '{}': {}", path, file_opt.error());
        return "";
    }
    auto content_opt = file_opt.value().read_whole();
    if (!content_opt) {
        spdlog::debug("(test/read) can't read '{}': {}", path, content_opt.error());
        return "";
    }
    auto &content = content_opt.value();
    auto view = std::string_view(reinterpret_cast<char *>(content.data()), content.size());
    return std::string(view);
}

std::string read_file(const utils::path_t &path) {
    auto buffer = std::array<std::byte, 1024 * 4>{};
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    return read_file(path.get_view(allocator));
}

void write_file(const utils::poly_path_view_t &path_, std::string_view content) {
    auto ec = sys::error_code{};
    utils::create_directories(path_.get_parent(), ec);
    if (ec) {
        throw ec.message();
    }

    auto opt = utils::io_stream_t::open_write(path_, content.size());
    if (opt.has_error()) {
        auto& ec = opt.assume_error();
        std::cout << fmt::format("(test/write) can't open {}: {} ", path_, ec);
        std::abort();
    }
    if (content.size()) {
        auto ok = opt.assume_value().stream.write(content);
        if (!ok) {
            spdlog::error("cannot write to '{}': {}", path_, ok.assume_error());
        }
    }
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

path_guard_t unique_path() {
    auto n = dist(rd);
    auto view = utils::bytes_view_t(reinterpret_cast<const unsigned char *>(&n), sizeof(n));
    auto random_name = utils::base32::encode(view);
    std::transform(random_name.begin(), random_name.end(), random_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    auto buffer = std::array<std::byte, 1024 * 32>();
    auto pool = std::pmr::monotonic_buffer_resource(buffer.data(), buffer.size());
    auto allocator = std::pmr::polymorphic_allocator<char>(&pool);
    auto name = fmt::format("tmp-{}", random_name);
    auto path = std::filesystem::current_path() / name;
    return path_guard_t(path.wstring());
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

bool exists(const utils::poly_path_view_t &path) {
    auto ec = sys::error_code{};
    auto r = utils::exists(path, ec);
    if (ec) {
        spdlog::debug("existance of '{}' failed with: {}", path, ec);
    }
    return r;
}

void chmod(const utils::poly_path_view_t &path, std::uint32_t mode) {
    auto ec = sys::error_code{};
    utils::chmod(path, mode, ec);
    if (ec) {
        spdlog::error("chmod '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
}

std::size_t create_directories(const utils::poly_path_view_t &path) {
    auto ec = sys::error_code{};
    auto r = utils::create_directories(path, ec);
    if (ec) {
        spdlog::error("create_directories '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
    return r;
}

static utils::stats_t get_stats(const utils::poly_path_view_t &path) {
    auto ec = sys::error_code{};
    auto r = utils::get_stats(path, ec);
    if (ec) {
        spdlog::error("get_stats '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
    return r;
}

std::uint32_t permissions(const utils::poly_path_view_t &path) {
    return get_stats(path).permissions;
}

std::int64_t file_size(const utils::poly_path_view_t &path) {
    return get_stats(path).file_size;
}

std::int64_t last_write_time(const utils::poly_path_view_t &path) {
    return get_stats(path).modification;
}

bool is_directory(const utils::poly_path_view_t &path) {
    return get_stats(path).file_type == utils::file_type_t::DIRECTORY;
}

bool is_symlink(const utils::poly_path_view_t &path) {
    return get_stats(path).file_type == utils::file_type_t::SYMLINK;
}

utils::poly_string_t read_symlink(const utils::poly_path_view_t &target) {
    auto ec = sys::error_code{};
    auto r = utils::read_symlink(target, ec);
    if (ec) {
        spdlog::error("read_symlink '{}': {}", target, ec);
        throw std::runtime_error(ec.message());
    }
    return r;
}

void create_symlink(const utils::path_base_t &target, const utils::path_base_t &path) {
    auto ec = sys::error_code{};
    utils::create_symlink(target, path, ec);
    if (ec) {
        spdlog::error("create_symlink '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
}

void rename(const utils::path_base_t &from, const utils::poly_path_view_t &to) {
    auto ec = sys::error_code{};
    utils::rename(from, to, ec);
    if (ec) {
        spdlog::error("rename '{}' -> '{}': {}", from, to, ec);
        throw std::runtime_error(ec.message());
    }
}

void last_write_time(const utils::poly_path_view_t &path, std::int64_t time) {
    auto ec = sys::error_code{};
    utils::last_write_time(path, time, ec);
    if (ec) {
        spdlog::error("last_write_time '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
}

void remove_all(const utils::poly_path_view_t &path) {
    auto ec = sys::error_code{};
    utils::remove_all(path, ec);
    if (ec) {
        spdlog::error("remove_all '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
}

void remove(const utils::poly_path_view_t &path) {
    auto ec = sys::error_code{};
    utils::remove_file(path, ec);
    if (ec) {
        spdlog::error("remove '{}': {}", path, ec);
        throw std::runtime_error(ec.message());
    }
}

} // namespace syncspirit::test
