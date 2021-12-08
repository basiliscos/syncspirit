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
#    fixture.cpp
#    sample_db.cpp
#    sample_peer.cpp
    test_supervisor.cpp
    test-db.cpp
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

add_executable(020-generic-map 020-generic-map.cpp)
target_link_libraries(020-generic-map syncspirit_test_lib)
add_test(020-generic-map "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/020-generic-map")

add_executable(025-device_id 025-device_id.cpp)
target_link_libraries(025-device_id syncspirit_test_lib)
add_test(025-device_id "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/025-device_id")

add_executable(029-diff-generic 029-diff-generic.cpp)
target_link_libraries(029-diff-generic syncspirit_test_lib)
add_test(029-diff-generic "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/029-diff-generic")

add_executable(030-diff-load-cluster 030-diff-load-cluster.cpp)
target_link_libraries(030-diff-load-cluster syncspirit_test_lib)
add_test(030-diff-load-cluster "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/030-diff-load-cluster")

add_executable(031-diff-cluster_update 031-diff-cluster_update.cpp)
target_link_libraries(031-diff-cluster_update syncspirit_test_lib)
add_test(031-diff-cluster_update "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/031-diff-cluster_update")

add_executable(032-diff-modify 032-diff-modify.cpp)
target_link_libraries(032-diff-modify syncspirit_test_lib)
add_test(032-diff-modify "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/032-diff-modify")

add_executable(033-diffs-trivial 033-diffs-trivial.cpp)
target_link_libraries(033-diffs-trivial syncspirit_test_lib)
add_test(033-diffs-trivial "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/033-diffs-trivial")

add_executable(034-diff-new_file 034-diff-new_file.cpp)
target_link_libraries(034-diff-new_file syncspirit_test_lib)
add_test(034-diff-new_file "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/034-diff-new_file")

add_executable(035-diff-local_update 035-diff-local_update.cpp)
target_link_libraries(035-diff-local_update syncspirit_test_lib)
add_test(035-diff-local_update "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/035-diff-local_update")

add_executable(036-diff-blocks 036-diff-blocks.cpp)
target_link_libraries(036-diff-blocks syncspirit_test_lib)
add_test(036-diff-blocks "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/036-diff-blocks")

add_executable(037-diff-update_folder 037-diff-update_folder.cpp)
target_link_libraries(037-diff-update_folder syncspirit_test_lib)
add_test(037-diff-update_folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/037-diff-update_folder")

add_executable(050-file_iterator 050-file_iterator.cpp)
target_link_libraries(050-file_iterator syncspirit_test_lib)
add_test(050-file_iterator "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/050-file_iterator")

add_executable(051-block_iterator 051-block_iterator.cpp)
target_link_libraries(051-block_iterator syncspirit_test_lib)
add_test(051-block_iterator "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/051-block_iterator")

#add_executable(051-model-file_info 051-model-file_info.cpp)
#target_link_libraries(051-model-file_info syncspirit_test_lib)
#add_test(051-model-file_info "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/051-model-file_info")

#add_executable(052-model-folder 052-model-folder.cpp)
#target_link_libraries(052-model-folder syncspirit_test_lib)
#add_test(052-model-folder "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/052-model-folder")

#add_executable(055-model-cluster 055-model-cluster.cpp)
#target_link_libraries(055-model-cluster syncspirit_test_lib)
#add_test(055-model-cluster "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/055-model-cluster")

add_executable(060-bep 060-bep.cpp)
target_link_libraries(060-bep syncspirit_test_lib)
add_test(060-bep "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/060-bep")

add_executable(070-db 070-db.cpp)
target_link_libraries(070-db syncspirit_test_lib)
add_test(070-db "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/070-db")

add_executable(071-fs_actor 071-fs_actor.cpp)
target_link_libraries(071-fs_actor syncspirit_test_lib)
add_test(071-fs_actor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/071-fs_actor")

#add_executable(080-fs 080-fs.cpp)
#target_link_libraries(080-fs syncspirit_test_lib)
#add_test(080-fs "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/080-fs")

#add_executable(081-hasher 081-hasher.cpp)
#target_link_libraries(081-hasher syncspirit_test_lib)
#add_test(081-hasher "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/081-hasher")

#add_executable(082-db-actor 082-db-actor.cpp)
#target_link_libraries(082-db-actor syncspirit_test_lib)
#add_test(082-db-actor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/082-db-actor")

#add_executable(083-scan-actor 083-scan-actor.cpp)
#target_link_libraries(083-scan-actor syncspirit_test_lib)
#add_test(083-scan-actor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/083-scan-actor")

#add_executable(125-cluster-supervisor 125-cluster-supervisor.cpp)
#target_link_libraries(125-cluster-supervisor syncspirit_test_lib)
#add_test(125-cluster-supervisor "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/125-cluster-supervisor")

#add_executable(130-controller-cluster 130-controller-cluster.cpp)
#target_link_libraries(130-controller-cluster syncspirit_test_lib)
#add_test(130-controller-cluster "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/130-controller-cluster")

#add_executable(131-controller-indexupdate 131-controller-indexupdate.cpp)
#target_link_libraries(131-controller-indexupdate syncspirit_test_lib)
#add_test(131-controller-indexupdate "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/131-controller-indexupdate")

#add_executable(132-controller-resume 132-controller-resume.cpp)
#target_link_libraries(132-controller-resume syncspirit_test_lib)
#add_test(132-controller-resume "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/132-controller-resume")

#add_executable(133-controller-download 133-controller-download.cpp)
#target_link_libraries(133-controller-download syncspirit_test_lib)
#add_test(133-controller-download "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/133-controller-download")

