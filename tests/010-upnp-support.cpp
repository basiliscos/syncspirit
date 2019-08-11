#include "catch.hpp"
#include "utils/upnp_support.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace sys = boost::system;
namespace fs = boost::filesystem;

using namespace syncspirit::utils;

TEST_CASE("parse IGD description", "[support]") {
    std::string test_file = "/tests/data/49652gatedesc.xml";
    auto parent = fs::path(__FILE__).parent_path().parent_path();
    auto file_path = fs::path(parent.string() + test_file);
    sys::error_code ec;
    auto filesize = fs::file_size(file_path, ec);
    REQUIRE(!ec);
    std::ifstream in;
    in.open(file_path.string(), std::ios::binary);
    std::vector<std::uint8_t> buffer(filesize);
    REQUIRE(in.read(reinterpret_cast<char*>(buffer.data()), filesize));
    auto wan_service = parse_igd(buffer.data(), filesize);
    REQUIRE(wan_service);
    REQUIRE(wan_service.value().control_path == "/upnp/control/WANIPConn1");
    REQUIRE(wan_service.value().description_path == "/gateconnSCPD.xml");
}
