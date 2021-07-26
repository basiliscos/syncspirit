//
// Copyright (c) 2019-2021 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//


#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "test-utils.h"
#include "model/device_id.h"

int main(int argc, char *argv[]) {
    return Catch::Session().run(argc, argv);
}


namespace syncspirit::test {

boost::filesystem::path file_path(const char* test_file) {
    auto self_file = __FILE__;
    bfs::path self(self_file);
    self.remove_filename();
    return self /  test_file;
}

std::string read_file(const bfs::path& path) {
    sys::error_code ec;
    auto filesize = bfs::file_size(path, ec);
    auto file_path_c = path.c_str();
    auto in = fopen(file_path_c, "rb");
    if (!in) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path_c << " : " << ec.message() << "\n";
        std::abort();
    }
    assert(in);
    std::vector<char> buffer(filesize, 0);
    auto r = fread(buffer.data(), filesize, 1, in);
    assert(r == 1);
    fclose(in);
    return std::string(buffer.data(), filesize);
}

std::string read_file(const char* test_file) {
    return read_file(file_path(test_file));
}

void write_file(const bfs::path& path, std::string_view content) {
    bfs::create_directories(path.parent_path());
    auto file_path_c = path.c_str();
    auto out = fopen(file_path_c, "wb");
    if (!out) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path_c << " : " << ec.message() << "\n";
        std::abort();
    }
    if (content.size()) {
        auto r = fwrite(content.data(), content.size(), 1, out);
        assert(r);
    }
    fclose(out);
}

std::string device_id2sha256(const char* device_id) {
    return model::device_id_t::from_string(device_id).value().get_sha256();
}



}
