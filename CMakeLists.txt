cmake_minimum_required (VERSION 3.19)
project (syncspirit VERSION 0.4.0)

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
find_package(Protobuf REQUIRED)
find_package(ZLIB REQUIRED)
find_package(rotor COMPONENTS asio thread REQUIRED)
find_package(lz4 REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
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
set(Protobuf_IMPORT_DIRS ${syncspirit_SOURCE_DIR}/src/protobuf)
protobuf_generate_cpp(
    PROTO_SRCS PROTO_HDRS
    EXPORT_MACRO SYNCSPIRIT_API
    src/protobuf/bep.proto
    src/protobuf/structs.proto
)
add_library(syncspirit_core
    ${PROTO_SRCS}
    src/constants.cpp
    src/config/utils.cpp
    src/db/cursor.cpp
    src/db/error_code.cpp
    src/db/prefix.cpp
    src/db/transaction.cpp
    src/db/utils.cpp
    src/fs/chunk_iterator.cpp
    src/fs/file.cpp
    src/fs/file_actor.cpp
    src/fs/fs_supervisor.cpp
    src/fs/new_chunk_iterator.cpp
    src/fs/scan_actor.cpp
    src/fs/scan_scheduler.cpp
    src/fs/scan_task.cpp
    src/fs/utils.cpp
    src/hasher/hasher_actor.cpp
    src/hasher/hasher_proxy_actor.cpp
    src/hasher/hasher_supervisor.cpp
    src/model/diff/apply_controller.cpp
    src/model/diff/block_diff.cpp
    src/model/diff/cluster_diff.cpp
    src/model/diff/cluster_visitor.cpp
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
    src/model/diff/load/devices.cpp
    src/model/diff/load/file_infos.cpp
    src/model/diff/load/folder_infos.cpp
    src/model/diff/load/folders.cpp
    src/model/diff/load/ignored_devices.cpp
    src/model/diff/load/ignored_folders.cpp
    src/model/diff/load/load_cluster.cpp
    src/model/diff/load/pending_devices.cpp
    src/model/diff/load/pending_folders.cpp
    src/model/diff/local/blocks_availability.cpp
    src/model/diff/local/custom.cpp
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
    src/model/diff/modify/append_block.cpp
    src/model/diff/modify/block_ack.cpp
    src/model/diff/modify/block_rej.cpp
    src/model/diff/modify/block_transaction.cpp
    src/model/diff/modify/clone_block.cpp
    src/model/diff/modify/finish_file.cpp
    src/model/diff/modify/generic_remove.cpp
    src/model/diff/modify/lock_file.cpp
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
    src/model/diff/peer/update_folder.cpp
    src/model/misc/block_iterator.cpp
    src/model/misc/error_code.cpp
    src/model/misc/file_block.cpp
    src/model/misc/file_iterator.cpp
    src/model/misc/orphaned_blocks.cpp
    src/model/misc/resolver.cpp
    src/model/misc/sequencer.cpp
    src/model/misc/updates_streamer.cpp
    src/model/misc/uuid.cpp
    src/model/block_info.cpp
    src/model/cluster.cpp
    src/model/device.cpp
    src/model/device_id.cpp
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
    src/net/messages.cpp
    src/net/net_supervisor.cpp
    src/net/peer_actor.cpp
    src/net/peer_supervisor.cpp
    src/net/relay_actor.cpp
    src/net/resolver_actor.cpp
    src/net/names.cpp
    src/net/sink_actor.cpp
    src/net/ssdp_actor.cpp
    src/net/upnp_actor.cpp
    src/proto/bep_support.cpp
    src/proto/discovery_support.cpp
    src/proto/luhn32.cpp
    src/proto/relay_support.cpp
    src/proto/upnp_support.cpp
    src/transport/stream.cpp
    src/transport/http.cpp
    src/utils/base32.cpp
    src/utils/beast_support.cpp
    src/utils/dns.cpp
    src/utils/error_code.cpp
    src/utils/location.cpp
    src/utils/log.cpp
    src/utils/log-setup.cpp
    src/utils/network_interface.cpp
    src/utils/platform.cpp
    src/utils/time.cpp
    src/utils/tls.cpp
    src/utils/uri.cpp
)
generate_export_header(syncspirit_core
    EXPORT_MACRO_NAME SYNCSPIRIT_API
    EXPORT_FILE_NAME include/syncspirit-export.h
)
target_compile_options(syncspirit_core PRIVATE
    -include ${CMAKE_CURRENT_BINARY_DIR}/include/syncspirit-export.h
)

# windows xp patch required:
# -        CancelIoEx(ior->overlapped_fd, &item->ov);
# +        CancelIo(ior->overlapped_fd);
FetchContent_Declare(MDBX
    URL https://libmdbx.dqdkfa.ru/release/libmdbx-amalgamated-0.13.4.tar.xz
    URL_HASH SHA256=86df30ca2231c9b3ad71424bb829dca9041947f5539d4295030c653d4982c1be
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


# To support WinXP there is need of that commit. When cares will be released
# and pushed to conan center, the ready receive capn be used
FetchContent_Declare(C_ARES
    GIT_REPOSITORY https://github.com/c-ares/c-ares.git
    GIT_TAG        7978cf75a82f9860da241227169a4d7f1d1f549a
    # URL https://github.com/c-ares/c-ares/archive/refs/tags/v1.34.4.tar.gz
    # URL_HASH SHA256=a35f7c4cdbdfaf0a69a9a50029e95ffe403daf605fade05c649d18333592222d
)
    set(CARES_STATIC      true  CACHE BOOL "CARES_STATIC")
    set(CARES_SHARED      false CACHE BOOL "CARES_SHARED")
    set(CARES_STATIC_PIC  true  CACHE BOOL "CARES_STATIC_PIC")
    set(CARES_BUILD_TOOLS false CACHE BOOL "CARES_BUILD_TOOLS")
    set(CARES_THREADS     false CACHE BOOL "CARES_THREADS")
FetchContent_MakeAvailable(C_ARES)

target_include_directories(syncspirit_core PUBLIC
    ${Protobuf_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncspirit_core
    PUBLIC
        protobuf::libprotobuf
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
        c-ares::cares
)
install_executable("syncspirit_core")

if (SYNCSPIRIT_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()

add_subdirectory("src/ui-daemon")
add_subdirectory("src/ui-fltk")
