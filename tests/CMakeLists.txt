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
    test_supervisor.cpp
    test-main.cpp
)
target_link_libraries(syncspirit_test_lib syncspirit_lib)
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

add_executable(012-base32 012-base32.cpp)
target_link_libraries(012-base32 syncspirit_test_lib)
add_test(012-base32 "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/012-base32")

add_executable(013-global-discovery 013-global-discovery.cpp)
target_link_libraries(013-global-discovery syncspirit_test_lib)
add_test(013-global-discovery "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/013-global-discovery")

add_executable(014-configuration 014-configuration.cpp)
target_link_libraries(014-configuration syncspirit_test_lib)
add_test(014-configuration "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/014-configuration")

add_executable(015-logger 015-logger.cpp)
target_link_libraries(015-logger syncspirit_test_lib)
add_test(015-logger "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/015-logger")

add_executable(050-model-device_id 050-model-device_id.cpp)
target_link_libraries(050-model-device_id syncspirit_test_lib)
add_test(050-model-device_id "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/050-model-device_id")

add_executable(055-model-cluster 055-model-cluster.cpp)
target_link_libraries(055-model-cluster syncspirit_test_lib)
add_test(055-model-cluster "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/055-model-cluster")

add_executable(060-bep 060-bep.cpp)
target_link_libraries(060-bep syncspirit_test_lib)
add_test(060-bep "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/060-bep")

add_executable(070-db 070-db.cpp)
target_link_libraries(070-db syncspirit_test_lib)
add_test(070-db "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/070-db")

add_executable(080-fs 080-fs.cpp)
target_link_libraries(080-fs syncspirit_test_lib)
add_test(080-fs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/080-fs")

add_executable(130-controller 130-controller.cpp)
target_link_libraries(130-controller syncspirit_test_lib)
add_test(130-controller "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/130-controller")

