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

add_executable(syncspirit
    src/configuration.cpp
    src/main.cpp
    src/net/global_discovery_actor.cpp
    src/net/net_supervisor.cpp
    src/net/upnp_actor.cpp
    src/utils/error_code.cpp
    src/utils/uri.cpp
    src/utils/upnp_support.cpp
)

add_subdirectory("lib/rotor")
add_subdirectory("lib/fmt")

target_include_directories(syncspirit PUBLIC
    ${syncspirit_SOURCE_DIR}/lib/rotor/include
    ${syncspirit_SOURCE_DIR}/lib/spdlog/include
    ${syncspirit_SOURCE_DIR}/lib/fmt/include
    ${syncspirit_SOURCE_DIR}/lib/json/include
    ${syncspirit_SOURCE_DIR}/lib/outcome/include
)

target_link_libraries(syncspirit
    rotor_asio
    fmt::fmt
    ${Boost_LIBRARIES}
    OpenSSL::SSL
)
