enable_testing()
include(GenerateExportHeader)

find_package(
    Boost
    COMPONENTS
        filesystem
        program_options
        system
    REQUIRED
)
find_package(OpenSSL REQUIRED)
find_package(Catch2 REQUIRED)

if(MINGW)
    ENABLE_LANGUAGE(RC)
endif()

add_library(syncspirit_test_lib
    $<$<PLATFORM_ID:Windows>:win32-resource.rc>
    test_supervisor.cpp
    test-db.cpp
    test-utils.cpp
    diff-builder.cpp
    $<$<PLATFORM_ID:Windows>:wsock32>
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:iphlpapi>
)
target_link_libraries(syncspirit_test_lib syncspirit_lib Catch2::Catch2WithMain)
target_include_directories(syncspirit_test_lib PUBLIC
    "${syncspirit_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
generate_export_header(syncspirit_test_lib
    EXPORT_MACRO_NAME SYNCSPIRIT_TEST_API
    EXPORT_FILE_NAME include/syncspirit-test-export.h
)

add_custom_command(TARGET syncspirit_test_lib PRE_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory
                   ${CMAKE_SOURCE_DIR}/tests/data ${CMAKE_BINARY_DIR}/data)

add_executable(009-uri 009-uri.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(009-uri syncspirit_test_lib)
add_test(009-uri "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/009-uri")

add_executable(010-upnp-support 010-upnp-support.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(010-upnp-support syncspirit_test_lib)
add_test(010-upnp-support "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/010-upnp-support")

add_executable(011-tls-util 011-tls-util.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(011-tls-util syncspirit_test_lib)
add_test(011-tls-util "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/011-tls-util")

add_executable(012-base32 012-base32.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(012-base32 syncspirit_test_lib)
add_test(012-base32 "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/012-base32")

add_executable(013-global-discovery 013-global-discovery.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(013-global-discovery syncspirit_test_lib)
add_test(013-global-discovery "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/013-global-discovery")

add_executable(014-configuration 014-configuration.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(014-configuration syncspirit_test_lib)
add_test(014-configuration "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/014-configuration")

add_executable(015-logger 015-logger.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(015-logger syncspirit_test_lib)
add_test(015-logger "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/015-logger")

add_executable(016-relay-support 016-relay-support.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(016-relay-support syncspirit_test_lib)
add_test(016-relay-support "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/016-relay-support")

add_executable(017-fs-utils 017-fs-utils.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(017-fs-utils syncspirit_test_lib)
add_test(017-fs-utils "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/017-fs-utils")

add_executable(020-generic-map 020-generic-map.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(020-generic-map syncspirit_test_lib)
add_test(020-generic-map "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/020-generic-map")

add_executable(025-device_id 025-device_id.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(025-device_id syncspirit_test_lib)
add_test(025-device_id "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/025-device_id")

add_executable(029-diff-generic 029-diff-generic.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(029-diff-generic syncspirit_test_lib)
add_test(029-diff-generic "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/029-diff-generic")

add_executable(030-diff-load-cluster 030-diff-load-cluster.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(030-diff-load-cluster syncspirit_test_lib)
add_test(030-diff-load-cluster "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/030-diff-load-cluster")

add_executable(031-diff-cluster_update 031-diff-cluster_update.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(031-diff-cluster_update syncspirit_test_lib)
add_test(031-diff-cluster_update "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/031-diff-cluster_update")

add_executable(032-diff-modify 032-diff-modify.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(032-diff-modify syncspirit_test_lib)
add_test(032-diff-modify "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/032-diff-modify")

add_executable(033-diffs-trivial 033-diffs-trivial.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(033-diffs-trivial syncspirit_test_lib)
add_test(033-diffs-trivial "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/033-diffs-trivial")

add_executable(034-diff-local_update 034-diff-local_update.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(034-diff-local_update syncspirit_test_lib)
add_test(034-diff-local_update "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/034-diff-local_update")

add_executable(036-diff-blocks 036-diff-blocks.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(036-diff-blocks syncspirit_test_lib)
add_test(036-diff-blocks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/036-diff-blocks")

add_executable(037-diff-update_folder 037-diff-update_folder.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(037-diff-update_folder syncspirit_test_lib)
add_test(037-diff-update_folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/037-diff-update_folder")

add_executable(038-diff-clone_finish-file 038-diff-clone_finish-file.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(038-diff-clone_finish-file syncspirit_test_lib)
add_test(038-diff-clone_finish-file "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/038-diff-clone_finish-file")

add_executable(050-file_iterator 050-file_iterator.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(050-file_iterator syncspirit_test_lib)
add_test(050-file_iterator "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/050-file_iterator")

add_executable(051-block_iterator 051-block_iterator.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(051-block_iterator syncspirit_test_lib)
add_test(051-block_iterator "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/051-block_iterator")

add_executable(052-file_info 052-file_info.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(052-file_info syncspirit_test_lib)
add_test(052-file_info "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/052-file_info")

add_executable(053-scan_task 053-scan_task.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(053-scan_task syncspirit_test_lib)
add_test(053-scan_task "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/053-scan_task")

add_executable(054-updates_streamer 054-updates_streamer.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(054-updates_streamer syncspirit_test_lib)
add_test(054-updates_streamer "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/054-updates_streamer")

add_executable(060-bep 060-bep.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(060-bep syncspirit_test_lib)
add_test(060-bep "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/060-bep")

add_executable(070-db 070-db.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(070-db syncspirit_test_lib)
add_test(070-db "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/070-db")

add_executable(071-fs_actor 071-fs_actor.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(071-fs_actor syncspirit_test_lib)
add_test(071-fs_actor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/071-fs_actor")

add_executable(072-global_discovery 072-global_discovery.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(072-global_discovery syncspirit_test_lib)
add_test(072-global_discovery "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/072-global_discovery")

add_executable(073-dialer 073-dialer.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(073-dialer syncspirit_test_lib)
add_test(073-dialer "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/073-dialer")

add_executable(074-hasher 074-hasher.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(074-hasher syncspirit_test_lib)
add_test(074-hasher "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/074-hasher")

add_executable(075-controller 075-controller.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(075-controller syncspirit_test_lib)
add_test(075-controller "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/075-controller")

add_executable(076-scan_actor 076-scan_actor.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(076-scan_actor syncspirit_test_lib)
add_test(076-scan_actor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/076-scan_actor")

add_executable(077-initiator 077-initiator.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(077-initiator syncspirit_test_lib)
add_test(077-initiator "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/077-initiator")

add_executable(078-relay 078-relay.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(078-relay syncspirit_test_lib)
add_test(078-relay "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/078-relay")

add_executable(079-peer 079-peer.cpp $<$<PLATFORM_ID:Windows>:win32-resource.rc>)
target_link_libraries(079-peer syncspirit_test_lib)
add_test(079-peer "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/079-peer")
