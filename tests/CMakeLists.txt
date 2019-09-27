enable_testing()

find_package(
    Boost
    COMPONENTS
        filesystem
        program_options
        system
    REQUIRED
)
find_package(OpenSSL REQUIRED)

add_library(syncspirit_test_lib
    test-main.cpp
)
target_link_libraries(syncspirit_test_lib
    syncspirit_lib
    rotor_asio
    pugixml
    fmt::fmt
    ${Boost_LIBRARIES}
    OpenSSL::SSL
)
target_include_directories(syncspirit_test_lib PUBLIC
    "${syncspirit_SOURCE_DIR}/src"
)

add_executable(009-uri 009-uri.cpp)
target_link_libraries(009-uri syncspirit_test_lib)
add_test(009-uri "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/009-uri")

add_executable(010-upnp-support 010-upnp-support.cpp)
target_link_libraries(010-upnp-support syncspirit_test_lib)
add_test(010-upnp-support "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/010-upnp-support")

add_executable(011-tls-util 011-tls-util.cpp)
target_link_libraries(011-tls-util syncspirit_test_lib)
add_test(011-tls-util "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/011-tls-util")

