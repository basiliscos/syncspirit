cmake_minimum_required (VERSION 3.19)
project (syncspirit VERSION 0.4.4)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)

cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0077 NEW)
cmake_policy(SET CMP0091 NEW)
cmake_policy(SET CMP0135 NEW)

include(GenerateExportHeader)
include (FetchContent)

if (WIN32)
    # set(CMAKE_CROSSCOMPILING on)
    # set(CMAKE_CROSSCOMPILING_EMULATOR "/usr/bin/wine")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE True)

#set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
#set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

find_package(
    Boost
    COMPONENTS
        nowide
        program_options
        system
        url
    REQUIRED
)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(rotor COMPONENTS asio thread REQUIRED)
find_package(lz4 REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(protopuf REQUIRED)
find_package(pugixml REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(nlohmann_json REQUIRED)
#find_package(c-ares REQUIRED)

set(SYNCSPIRIT_VERSION "v${CMAKE_PROJECT_VERSION}")

option(SYNCSPIRIT_BUILD_TESTS "Enable building tests [default: OFF]"  OFF)

set(dist_dir "${syncspirit_BINARY_DIR}/dist")
add_custom_target("deploy")
if (WIN32)
    add_custom_target("deploy_deps"
        COMMAND
            python ${syncspirit_SOURCE_DIR}/misc/post-install-win32.py
                --binaries ${dist_dir}/*
                --dirs
                    `conan config home`
                    ${CMAKE_BINARY_DIR}
                    `dirname ${CMAKE_CXX_COMPILER}`/../../
        DEPENDS deploy
    )
endif()

function(install_executable target)
    install(TARGETS ${target} DESTINATION bin)
        add_custom_target("deploy_${target}")
        add_dependencies("deploy" "deploy_${target}")
        if (WIN32)
            get_target_property(target_type ${target} TYPE)
            if (target_type STREQUAL "EXECUTABLE")
                add_custom_command(TARGET deploy_${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E
                        copy
                            $<TARGET_FILE:${target}>
                            "${dist_dir}/$<TARGET_FILE_NAME:${target}>"
                )
            endif ()
        else()
            add_custom_command(TARGET deploy_${target} POST_BUILD
                COMMAND
                   cd ${syncspirit_BINARY_DIR} && sh
                    ${syncspirit_SOURCE_DIR}/misc/make-appimage.sh
                        $<TARGET_FILE:${target}>
            )
        endif()
endfunction()

configure_file(misc/syncspirit-config.h.in include/syncspirit-config.h @ONLY)

add_library(syncspirit_core
    src/constants.cpp
    src/bouncer/bouncer_actor.cpp
    src/config/utils.cpp
    src/db/cursor.cpp
    src/db/error_code.cpp
    src/db/prefix.cpp
    src/db/transaction.cpp
    src/db/utils.cpp
    src/fs/file.cpp
    src/fs/file_actor.cpp
    src/fs/file_cache.cpp
    src/fs/fs_supervisor.cpp
    src/fs/fs_slave.cpp
    src/fs/scan_scheduler.cpp
    src/fs/utils.cpp
    src/fs/task/noop.cpp
    src/fs/task/remove_file.cpp
    src/fs/task/rename_file.cpp
    src/fs/task/segment_iterator.cpp
    src/fs/task/scan_dir.cpp
    src/hasher/hasher_actor.cpp
    src/hasher/hasher_plugin.cpp
    src/hasher/hasher_supervisor.cpp
    src/model/diff/apply_controller.cpp
    src/model/diff/block_diff.cpp
    src/model/diff/cluster_diff.cpp
    src/model/diff/cluster_visitor.cpp
    src/model/diff/iterative_controller.cpp
    src/model/diff/advance/advance.cpp
    src/model/diff/advance/local_update.cpp
    src/model/diff/advance/remote_copy.cpp
    src/model/diff/advance/remote_win.cpp
    src/model/diff/contact/connect_request.cpp
    src/model/diff/contact/dial_request.cpp
    src/model/diff/contact/ignored_connected.cpp
    src/model/diff/contact/relay_connect_request.cpp
    src/model/diff/contact/peer_state.cpp
    src/model/diff/contact/unknown_connected.cpp
    src/model/diff/contact/update_contact.cpp
    src/model/diff/load/blocks.cpp
    src/model/diff/load/commit.cpp
    src/model/diff/load/devices.cpp
    src/model/diff/load/file_infos.cpp
    src/model/diff/load/folder_infos.cpp
    src/model/diff/load/folders.cpp
    src/model/diff/load/ignored_devices.cpp
    src/model/diff/load/ignored_folders.cpp
    src/model/diff/load/interrupt.cpp
    src/model/diff/load/load_cluster.cpp
    src/model/diff/load/pending_devices.cpp
    src/model/diff/load/pending_folders.cpp
    src/model/diff/load/remove_corrupted_files.cpp
    src/model/diff/local/blocks_availability.cpp
    src/model/diff/local/file_availability.cpp
    src/model/diff/local/io_failure.cpp
    src/model/diff/local/scan_finish.cpp
    src/model/diff/local/scan_request.cpp
    src/model/diff/local/scan_start.cpp
    src/model/diff/local/synchronization_finish.cpp
    src/model/diff/local/synchronization_start.cpp
    src/model/diff/modify/add_blocks.cpp
    src/model/diff/modify/add_remote_folder_infos.cpp
    src/model/diff/modify/add_ignored_device.cpp
    src/model/diff/modify/add_pending_device.cpp
    src/model/diff/modify/add_pending_folders.cpp
    src/model/diff/modify/block_ack.cpp
    src/model/diff/modify/block_transaction.cpp
    src/model/diff/modify/generic_remove.cpp
    src/model/diff/modify/mark_reachable.cpp
    src/model/diff/modify/remove_blocks.cpp
    src/model/diff/modify/remove_files.cpp
    src/model/diff/modify/remove_folder.cpp
    src/model/diff/modify/remove_folder_infos.cpp
    src/model/diff/modify/remove_peer.cpp
    src/model/diff/modify/remove_ignored_device.cpp
    src/model/diff/modify/remove_pending_device.cpp
    src/model/diff/modify/remove_pending_folders.cpp
    src/model/diff/modify/reset_folder_infos.cpp
    src/model/diff/modify/share_folder.cpp
    src/model/diff/modify/suspend_folder.cpp
    src/model/diff/modify/unshare_folder.cpp
    src/model/diff/modify/update_peer.cpp
    src/model/diff/modify/upsert_folder.cpp
    src/model/diff/modify/upsert_folder_info.cpp
    src/model/diff/peer/cluster_update.cpp
    src/model/diff/peer/rx_tx.cpp
    src/model/diff/peer/update_folder.cpp
    src/model/misc/augmentation.cpp
    src/model/misc/block_iterator.cpp
    src/model/misc/error_code.cpp
    src/model/misc/file_block.cpp
    src/model/misc/file_iterator.cpp
    src/model/misc/orphaned_blocks.cpp
    src/model/misc/path.cpp
    src/model/misc/path_cache.cpp
    src/model/misc/resolver.cpp
    src/model/misc/sequencer.cpp
    src/model/misc/updates_streamer.cpp
    src/model/misc/uuid.cpp
    src/model/block_info.cpp
    src/model/cluster.cpp
    src/model/device.cpp
    src/model/device_id.cpp
    src/model/device_state.cpp
    src/model/file_info.cpp
    src/model/folder.cpp
    src/model/folder_data.cpp
    src/model/folder_info.cpp
    src/model/ignored_device.cpp
    src/model/ignored_folder.cpp
    src/model/pending_device.cpp
    src/model/pending_folder.cpp
    src/model/remote_folder_info.cpp
    src/model/some_device.cpp
    src/model/version.cpp
    src/net/acceptor_actor.cpp
    src/net/cluster_supervisor.cpp
    src/net/controller_actor.cpp
    src/net/db_actor.cpp
    src/net/dialer_actor.cpp
    src/net/global_discovery_actor.cpp
    src/net/http_actor.cpp
    src/net/initiator_actor.cpp
    src/net/local_discovery_actor.cpp
    src/net/local_keeper.cpp
    src/net/net_supervisor.cpp
    src/net/peer_actor.cpp
    src/net/peer_supervisor.cpp
    src/net/relay_actor.cpp
    src/net/resolver_actor.cpp
    src/net/names.cpp
    src/net/ssdp_actor.cpp
    src/net/upnp_actor.cpp
    src/presentation/entity.cpp
    src/presentation/cluster_file_presence.cpp
    src/presentation/file_entity.cpp
    src/presentation/folder_entity.cpp
    src/presentation/folder_presence.cpp
    src/presentation/local_file_presence.cpp
    src/presentation/missing_file_presence.cpp
    src/presentation/orphans.cpp
    src/presentation/peer_file_presence.cpp
    src/presentation/presence.cpp
    src/proto/bep_support.cpp
    src/proto/discovery_support.cpp
    src/proto/luhn32.cpp
    src/proto/proto-helpers-bep.cpp
    src/proto/proto-helpers-db.cpp
    src/proto/relay_support.cpp
    src/proto/upnp_support.cpp
    src/transport/stream.cpp
    src/transport/http.cpp
    src/utils/base32.cpp
    src/utils/beast_support.cpp
    src/utils/bytes.cpp
    src/utils/dns.cpp
    src/utils/error_code.cpp
    src/utils/format.cpp
    src/utils/location.cpp
    src/utils/log.cpp
    src/utils/log-setup.cpp
    src/utils/network_interface.cpp
    src/utils/platform.cpp
    src/utils/time.cpp
    src/utils/tls.cpp
    src/utils/uri.cpp
)

set_source_files_properties(
    src/proto/bep_support.cpp
    src/proto/proto-helpers-bep.cpp
    src/proto/proto-helpers-db.cpp
    PROPERTIES COMPILE_FLAGS "-Os -fPIC"
)

generate_export_header(syncspirit_core
    EXPORT_MACRO_NAME SYNCSPIRIT_API
    EXPORT_FILE_NAME include/syncspirit-export.h
)
FetchContent_Declare(MDBX
    URL https://libmdbx.dqdkfa.ru/release/libmdbx-amalgamated-0.14.1.tar.xz
    URL_HASH SHA256=2edb8b59db5b2b021c7634010f8dbdbbf948eda609942dd300510c6e93bd5d25
    PATCH_COMMAND ${CMAKE_COMMAND} -E
        chdir ${CMAKE_CURRENT_BINARY_DIR}
        patch -u _deps/mdbx-src/mdbx.c -i ${CMAKE_CURRENT_SOURCE_DIR}/misc/mdbx-win32.patch
)
    if (WIN32)
        set(BUILD_SHARED_LIBS true CACHE BOOL "BUILD_SHARED_LIBS")
        set(MDBX_BUILD_SHARED_LIBRARY true CACHE BOOL "MDBX_BUILD_SHARED_LIBRARY")
    else()
        set(BUILD_SHARED_LIBS false CACHE BOOL "BUILD_SHARED_LIBS")
        set(MDBX_BUILD_SHARED_LIBRARY false CACHE BOOL "MDBX_BUILD_SHARED_LIBRARY")
    endif()
    set(MDBX_BUILD_TOOLS false  CACHE BOOL "MDBX_BUILD_TOOLS")
    set(MDBX_ENABLE_TESTS false CACHE BOOL "MDBX_ENABLE_TESTS")
    set(MDBX_BUILD_CXX false CACHE BOOL "MDBX_BUILD_CXX")
    set(MDBX_ALLOY_BUILD_DEFAULT true CACHE BOOL "MDBX_ALLOY_BUILD_DEFAULT")
    set(MDBX_ALLOY_BUILD true CACHE BOOL "MDBX_ALLOY_BUILD")
FetchContent_MakeAvailable(MDBX)

FetchContent_Declare(C_ARES
    URL https://github.com/c-ares/c-ares/releases/download/v1.34.5/c-ares-1.34.5.tar.gz
    URL_HASH SHA256=7d935790e9af081c25c495fd13c2cfcda4792983418e96358ef6e7320ee06346
)
    set(CARES_STATIC      true  CACHE BOOL "CARES_STATIC")
    set(CARES_SHARED      false CACHE BOOL "CARES_SHARED")
    set(CARES_STATIC_PIC  true  CACHE BOOL "CARES_STATIC_PIC")
    set(CARES_BUILD_TOOLS false CACHE BOOL "CARES_BUILD_TOOLS")
    set(CARES_THREADS     false CACHE BOOL "CARES_THREADS")
FetchContent_MakeAvailable(C_ARES)

FetchContent_Declare(UNI_ALGO
    URL https://github.com/uni-algo/uni-algo/archive/refs/tags/v1.2.0.tar.gz
    URL_HASH SHA256=f2a1539cd8635bc6088d05144a73ecfe7b4d74ee0361fabed6f87f9f19e74ca9
)
FetchContent_MakeAvailable(UNI_ALGO)


target_include_directories(syncspirit_core PUBLIC
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncspirit_core
    PUBLIC
        protopuf::protopuf
        rotor::asio
        rotor::thread
        pugixml::pugixml
        Boost::nowide
        Boost::program_options
        Boost::system
        Boost::url
        OpenSSL::SSL
        fmt::fmt
        spdlog::spdlog
        tomlplusplus::tomlplusplus
        nlohmann_json::nlohmann_json
        lz4::lz4
        $<IF:$<PLATFORM_ID:Windows>,mdbx,mdbx-static>
        $<$<PLATFORM_ID:Windows>:iphlpapi>
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:wsock32>
    PRIVATE
        ZLIB::ZLIB
        c-ares::cares
        uni-algo::uni-algo
)
install_executable("syncspirit_core")

if (SYNCSPIRIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()

add_subdirectory("src/ui-daemon")
add_subdirectory("src/ui-fltk")
