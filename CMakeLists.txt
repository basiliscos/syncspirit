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

add_library(syncspirit_lib
    src/configuration.cpp
    src/net/acceptor_actor.cpp
    src/net/global_discovery_actor.cpp
    src/net/http_actor.cpp
    src/net/net_supervisor.cpp
    src/net/ssdp_actor.cpp
    src/net/upnp_supervisor.cpp
    src/utils/error_code.cpp
    src/utils/uri.cpp
    src/utils/upnp_support.cpp
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
)

target_link_libraries(syncspirit
    syncspirit_lib
    rotor_asio
    pugixml
    fmt::fmt
    ${Boost_LIBRARIES}
    OpenSSL::SSL
)

enable_testing()
add_subdirectory("tests")
