cmake_minimum_required (VERSION 3.2)
project (syncspirit)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(
    Boost
    COMPONENTS
        filesystem
        program_options
        system
    REQUIRED
)
find_package(OpenSSL REQUIRED)
find_package(Protobuf REQUIRED)

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS src/proto/announce.proto)

add_library(syncspirit_lib
    src/configuration.cpp
    src/net/acceptor_actor.cpp
    src/net/global_discovery_actor.cpp
    src/net/http_actor.cpp
    src/net/net_supervisor.cpp
    src/net/peer_supervisor.cpp
    src/net/resolver_actor.cpp
    src/net/messages.cpp
    src/net/names.cpp
    src/net/ssdp_actor.cpp
    src/net/ssl.cpp
    src/net/upnp_actor.cpp
    src/proto/device_id.cpp
    src/proto/luhn32.cpp
    src/utils/base32.cpp
    src/utils/beast_support.cpp
    src/utils/error_code.cpp
    src/utils/location.cpp
    src/utils/tls.cpp
    src/utils/uri.cpp
    src/utils/upnp_support.cpp
    ${PROTO_SRCS}
)

add_executable(syncspirit
    src/main.cpp
)

add_subdirectory("lib/rotor")
add_subdirectory("lib/fmt")
add_subdirectory("lib/pugixml")

target_compile_definitions(pugixml PUBLIC "PUGIXML_NO_EXCEPTIONS")

target_include_directories(syncspirit_lib PUBLIC
    ${syncspirit_SOURCE_DIR}/lib/rotor/include
    ${syncspirit_SOURCE_DIR}/lib/spdlog/include
    ${syncspirit_SOURCE_DIR}/lib/fmt/include
    ${syncspirit_SOURCE_DIR}/lib/json/include
    ${syncspirit_SOURCE_DIR}/lib/pugixml/src
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${Protobuf_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(syncspirit
    syncspirit_lib
    rotor_asio
    pugixml
    fmt::fmt
    ${Boost_LIBRARIES}
    OpenSSL::SSL
    protobuf::libprotobuf-lite
)

enable_testing()
add_subdirectory("tests")
