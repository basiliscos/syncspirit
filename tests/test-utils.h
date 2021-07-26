#pragma once

#include <boost/filesystem.hpp>

namespace syncspirit::test {

namespace bfs = boost::filesystem;
namespace sys = boost::system;

struct path_guard_t {
    bfs::path& path;
    path_guard_t(bfs::path& path_): path{path_}{}
    ~path_guard_t() {
        bfs::remove_all(path);
    }
};

bfs::path file_path(const char* test_file);
std::string read_file(const char* test_file);
std::string read_file(const bfs::path& path);
void write_file(const bfs::path& path, std::string_view content);
std::string device_id2sha256(const char* device_id);

}
