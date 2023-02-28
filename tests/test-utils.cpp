// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2019-2023 Ivan Baidakou

#include "test-utils.h"
#include "model/device_id.h"
#include "structs.pb.h"
#include "db/prefix.h"

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
    auto filesize = bfs::file_size(path, ec);
    auto file_path = path.string();
    auto file_path_c = file_path.c_str();
    auto in = fopen(file_path_c, "rb");
    if (!in) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path_c << " : " << ec.message() << "\n";
        return "";
    }
    assert(in);
    std::vector<char> buffer(filesize, 0);
    auto r = fread(buffer.data(), filesize, 1, in);
    assert(r == 1);
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
    }
    fclose(out);
}

std::string device_id2sha256(std::string_view device_id) {
    return std::string(model::device_id_t::from_string(device_id).value().get_sha256());
}

model::device_ptr_t make_device(std::string_view device_id, std::string_view name) {
    auto id = model::device_id_t::from_string(device_id).value();
    return model::device_t::create(id, name).assume_value();
}

std::string hash_string(const std::string_view &hash) noexcept {
    auto r = std::string();
    r.reserve(hash.size() * 2);
    for (size_t i = 0; i < hash.size(); ++i) {
        char buff[3];
        sprintf(buff, "%02x", (unsigned char)hash[i]);
        r += std::string_view(buff, 2);
    }
    return r;
}

} // namespace syncspirit::test
