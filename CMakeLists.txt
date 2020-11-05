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

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    src/proto/bep.proto
)

add_library(syncspirit_lib
    src/configuration.cpp
    src/constants.cpp
    src/model/device_id.cpp
    src/net/acceptor_actor.cpp
    src/net/global_discovery_actor.cpp
    src/net/http_actor.cpp
    src/net/net_supervisor.cpp
    src/net/peer_actor.cpp
    src/net/peer_supervisor.cpp
    src/net/resolver_actor.cpp
    src/net/messages.cpp
    src/net/names.cpp
    src/net/ssdp_actor.cpp
    src/net/upnp_actor.cpp
    src/proto/bep_support.cpp
    src/proto/discovery_support.cpp
    src/proto/luhn32.cpp
    src/proto/upnp_support.cpp
    src/transport/base.cpp
    src/transport/tcp.cpp
    src/transport/tls.cpp
    src/utils/base32.cpp
    src/utils/beast_support.cpp
    src/utils/error_code.cpp
    src/utils/location.cpp
    src/utils/tls.cpp
    src/utils/uri.cpp
    ${PROTO_SRCS}
)

add_executable(syncspirit
    src/main.cpp
)

set(BUILD_BOOST_ASIO true CACHE BOOL "BUILD_BOOST_ASIO")
set(URIPARSER_BUILD_WCHAR_T false CACHE BOOL "URIPARSER_BUILD_WCHAR_T")
set(URIPARSER_BUILD_TOOLS false CACHE BOOL "URIPARSER_BUILD_TOOLS")
set(URIPARSER_BUILD_DOCS false CACHE BOOL "DURIPARSER_BUILD_DOCS")
set(URIPARSER_BUILD_TESTS false CACHE BOOL "URIPARSER_BUILD_TESTS")
set(URIPARSER_ENABLE_INSTALL false CACHE BOOL "URIPARSER_ENABLE_INSTALL")
set(BUILD_SHARED_LIBS false CACHE BOOL "BUILD_SHARED_LIBS")

add_subdirectory("lib/rotor")
add_subdirectory("lib/fmt")
add_subdirectory("lib/pugixml")
add_subdirectory("lib/uriparser")

target_compile_definitions(pugixml PUBLIC "PUGIXML_NO_EXCEPTIONS")

target_include_directories(syncspirit_lib PUBLIC
    ${syncspirit_SOURCE_DIR}/lib/rotor/include
    ${syncspirit_SOURCE_DIR}/lib/spdlog/include
    ${syncspirit_SOURCE_DIR}/lib/fmt/include
    ${syncspirit_SOURCE_DIR}/lib/json/include
    ${syncspirit_SOURCE_DIR}/lib/pugixml/src
    ${syncspirit_SOURCE_DIR}/lib/uriparser/include
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
    protobuf::libprotobuf
    uriparser
)

enable_testing()
add_subdirectory("tests")
