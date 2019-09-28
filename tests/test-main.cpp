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
    auto self = fs::path(__FILE__);
    auto parent = self.parent_path().parent_path();
    auto file_path = fs::path(parent.string() + test_file);
    sys::error_code ec;
    auto filesize = fs::file_size(file_path, ec);
    std::ifstream in;
    in.open(file_path.string(), std::ios::binary);
    std::vector<char> buffer(filesize);
    in.read(buffer.data(), filesize);
    return std::string(buffer.data(), filesize);
}


}
