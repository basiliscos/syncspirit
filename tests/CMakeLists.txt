enable_testing()
include(GenerateExportHeader)

find_package(
    Boost
    COMPONENTS
        system
    REQUIRED
)
find_package(OpenSSL REQUIRED)
find_package(Catch2 REQUIRED)

if(MINGW)
    ENABLE_LANGUAGE(RC)
endif()

add_library(syncspirit_test_lib SHARED
    $<$<PLATFORM_ID:Windows>:win32-resource.rc>
    managed_hasher.cpp
    test_peer.cpp
    test_supervisor.cpp
    test-db.cpp
    test-utils.cpp
    diff-builder.cpp
)
target_link_libraries(syncspirit_test_lib syncspirit_core
    Catch2::Catch2WithMain
    $<$<PLATFORM_ID:Windows>:wsock32>
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:iphlpapi>
)
target_include_directories(syncspirit_test_lib PUBLIC
    "${syncspirit_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
generate_export_header(syncspirit_test_lib
    EXPORT_MACRO_NAME SYNCSPIRIT_TEST_API
    EXPORT_FILE_NAME include/syncspirit-test-export.h
)

function(create_test source)
    cmake_path(REMOVE_EXTENSION source LAST_ONLY OUTPUT_VARIABLE target)

    add_executable(${target} ${source} $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
    target_link_libraries(${target} syncspirit_test_lib)
    add_test(
        NAME "${target}-test"
        COMMAND ${CMAKE_CROSSCOMPILING_EMULATOR} $<TARGET_FILE_NAME:${target}>
    )
endfunction()

add_custom_command(TARGET syncspirit_test_lib PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/tests/data ${CMAKE_BINARY_DIR}/data)

create_test(009-uri.cpp)
create_test(010-upnp-support.cpp)
create_test(011-tls-util.cpp)
create_test(012-base32.cpp)
create_test(013-global-discovery.cpp)
create_test(014-configuration.cpp)
create_test(015-logger.cpp)
create_test(016-relay-support.cpp)
create_test(017-fs-utils.cpp)
create_test(018-dns.cpp)
create_test(020-generic-map.cpp)
create_test(021-orphaned-blocks.cpp)
create_test(022-version.cpp)
create_test(023-device-state.cpp)
create_test(024-path.cpp)
create_test(025-device_id.cpp)
create_test(026-file_info.cpp)
create_test(027-cluster.cpp)
create_test(028-model-serialization.cpp)
create_test(029-diff-generic.cpp)
create_test(030-diff-load-cluster.cpp)
create_test(031-diff-cluster_update.cpp)
create_test(032-diff-modify.cpp)
create_test(033-diffs-trivial.cpp)
create_test(034-diff-local_update.cpp)
create_test(035-diff-update_folder.cpp)
create_test(036-diff-blocks.cpp)
create_test(037-diff-upsert_folder.cpp)
create_test(038-diff-clone_finish-file.cpp)
create_test(039-diff-remove-peer.cpp)
create_test(040-diff-contacts.cpp)
create_test(041-diff-remove-folder.cpp)
create_test(042-diff-interrupt.cpp)
create_test(043-diffs-scan.cpp)
create_test(045-diff-advance.cpp)
create_test(050-file_iterator.cpp)
create_test(051-block_iterator.cpp)
create_test(053-scan_task.cpp)
create_test(054-updates_streamer.cpp)
create_test(055-resolver.cpp)
create_test(056-block-request.cpp)
create_test(060-bep.cpp)
create_test(061-presentation.cpp)
create_test(070-db.cpp)
create_test(071-fs_actor.cpp)
create_test(072-global_discovery.cpp)
create_test(073-dialer.cpp)
create_test(074-hasher.cpp)
create_test(075-controller.cpp)
create_test(076-http.cpp)
create_test(077-initiator.cpp)
create_test(078-relay.cpp)
create_test(079-peer.cpp)
create_test(080-resolver.cpp)
create_test(085-scan-scheduler.cpp)
create_test(086-scan_actor.cpp)
create_test(110-file-n-controller-actors.cpp)
