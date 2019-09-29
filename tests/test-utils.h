#pragma once

#include <boost/filesystem.hpp>

namespace syncspirit::test {

namespace fs = boost::filesystem;
namespace sys = boost::system;

struct path_guard_t {
    fs::path& path;
    path_guard_t(fs::path& path_): path{path_}{}
    ~path_guard_t() {
        fs::remove(path);
    }
};

std::string read_file(const char* test_file);

}
