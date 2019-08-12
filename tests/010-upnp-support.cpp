#include "catch.hpp"
#include "utils/upnp_support.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit::utils;

std::string read_to_file(const char* test_file) {
    auto parent = fs::path(__FILE__).parent_path().parent_path();
    auto file_path = fs::path(parent.string() + test_file);
    sys::error_code ec;
    auto filesize = fs::file_size(file_path, ec);
    REQUIRE(!ec);
    std::ifstream in;
    in.open(file_path.string(), std::ios::binary);
    std::vector<char> buffer(filesize);
    REQUIRE(in.read(buffer.data(), filesize));
    return std::string(buffer.data(), filesize);
}

TEST_CASE("parse IGD description", "[support]") {
    auto xml = read_to_file("/tests/data/49652gatedesc.xml");
    auto wan_service = parse_igd(xml.c_str(), xml.size());
    REQUIRE(wan_service);
    REQUIRE(wan_service.value().control_path == "/upnp/control/WANIPConn1");
    REQUIRE(wan_service.value().description_path == "/gateconnSCPD.xml");
}
