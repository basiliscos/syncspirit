// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2025 Ivan Baidakou

#include "test-utils.h"
#include "model/device_id.h"
#include "utils/base32.h"
#include "utils/log-setup.h"
#include <random>
#include <cstdint>
#include <spdlog/sinks/stdout_color_sinks.h>

int main(int argc, char *argv[]) { return Catch::Session().run(argc, argv); }

namespace syncspirit::test {

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
    auto file_path = path.string();
    auto file_path_c = file_path.c_str();
    auto in = fopen(file_path_c, "rb");
    if (!in) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path_c << " : " << ec.message() << "\n";
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

void write_file(const bfs::path &path, std::string_view content) {
    bfs::create_directories(path.parent_path());
    auto file_path = path.string();
    auto out = fopen(file_path.c_str(), "wb");
    if (!out) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path << " : " << ec.message() << "\n";
        std::abort();
    }
    if (content.size()) {
        auto r = fwrite(content.data(), content.size(), 1, out);
        assert(r);
        (void)r;
    }
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

static model::diff::apply_controller_t apply_controller;

model::diff::apply_controller_t &get_apply_controller() { return apply_controller; }

void init_logging() {
    auto dist_sink = utils::create_root_logger();
    auto console_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    dist_sink->add_sink(console_sink);
}

static std::random_device rd;
static std::uniform_int_distribution<std::uint64_t> dist;

bfs::path unique_path() {
    auto n = dist(rd);
    auto view = utils::bytes_view_t(reinterpret_cast<const unsigned char *>(&n), sizeof(n));
    auto random_name = utils::base32::encode(view);
    std::transform(random_name.begin(), random_name.end(), random_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    auto name = std::string("tmp-") + random_name;
    return bfs::path(name);
}

utils::bytes_view_t as_bytes(std::string_view str) {
    auto ptr = (const unsigned char*) str.data();
    return {ptr, str.size()};
}

} // namespace syncspirit::test
