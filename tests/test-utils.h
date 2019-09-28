#pragma once

#include <boost/filesystem.hpp>

namespace syncspirit::test {
namespace fs = boost::filesystem;
namespace sys = boost::system;

std::string read_file(const char* test_file);

}
