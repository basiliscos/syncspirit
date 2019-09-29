//
// Copyright (c) 2019 Ivan Baidakou (basiliscos) (the dot dmol at gmail dot com)
//
// Distributed under the MIT Software License
//


#define CATCH_CONFIG_RUNNER
#include "catch.hpp"
#include "test-utils.h"

int main(int argc, char *argv[]) {
    return Catch::Session().run(argc, argv);
}


namespace syncspirit::test {

std::string read_file(const char* test_file) {
    auto self_file = __FILE__;
    fs::path self(self_file);
    self.remove_filename();
    fs::path file_path(self.string() + test_file);
    sys::error_code ec;
    auto filesize = fs::file_size(file_path, ec);
    auto file_path_c = file_path.c_str();
    auto in = fopen(file_path_c, "rb");
    if (!in) {
        auto ec = sys::error_code{errno, sys::generic_category()};
        std::cout << "can't open " << file_path_c << " : " << ec.message() << "\n";
        std::abort();
    }
    assert(in);
    std::vector<char> buffer(filesize, 0);
    assert(fread(buffer.data(), filesize, 1, in) == 1);
    fclose(in);
    return std::string(buffer.data(), filesize);
}


}
