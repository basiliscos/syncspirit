#pragma once

#include <boost/filesystem.hpp>

namespace syncspirit::test {

namespace fs = boost::filesystem;
namespace sys = boost::system;

struct path_guard_t {
    fs::path& path;
    path_guard_t(fs::path& path_): path{path_}{}
    ~path_guard_t() {
        fs::remove_all(path);
    }
};

std::string file_path(const char* test_file);
std::string read_file(const char* test_file);
std::string device_id2sha256(const char* device_id);

}
