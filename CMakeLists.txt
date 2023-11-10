cmake_minimum_required (VERSION 3.2)
project (syncspirit)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN 1)
cmake_policy(SET CMP0063 NEW)
cmake_policy(SET CMP0077 NEW)

include(GenerateExportHeader)
set(CMAKE_POSITION_INDEPENDENT_CODE True)

find_package(
    Boost
    COMPONENTS
        iostreams
        filesystem
        program_options
        system
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
find_package(uriparser REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(nlohmann_json REQUIRED)

set(SYNCSPIRIT_VERSION "v0.3.0")

configure_file(misc/syncspirit-config.h.in include/syncspirit-config.h @ONLY)
set(Protobuf_IMPORT_DIRS ${syncspirit_SOURCE_DIR}/src/protobuf)
protobuf_generate_cpp(
    PROTO_SRCS PROTO_HDRS
    EXPORT_MACRO SYNCSPIRIT_API
    src/protobuf/bep.proto
    src/protobuf/structs.proto
)
add_library(syncspirit_pb ${PROTO_SRCS})
generate_export_header(syncspirit_pb
    EXPORT_MACRO_NAME SYNCSPIRIT_API
    EXPORT_FILE_NAME include/syncspirit-export.h
)
target_compile_options(syncspirit_pb PRIVATE
    -include ${CMAKE_CURRENT_BINARY_DIR}/include/syncspirit-export.h
)
target_include_directories(syncspirit_pb PUBLIC
    ${Protobuf_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)
target_link_libraries(syncspirit_pb protobuf::libprotobuf)

add_library(syncspirit_lib
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
    src/fs/scan_task.cpp
    src/fs/utils.cpp
    src/hasher/hasher_actor.cpp
    src/hasher/hasher_proxy_actor.cpp
    src/hasher/hasher_supervisor.cpp
    src/model/diff/aggregate.cpp
    src/model/diff/base_diff.cpp
    src/model/diff/block_diff.cpp
    src/model/diff/block_visitor.cpp
    src/model/diff/contact_diff.cpp
    src/model/diff/contact_visitor.cpp
    src/model/diff/cluster_diff.cpp
    src/model/diff/cluster_visitor.cpp
    src/model/diff/load/blocks.cpp
    src/model/diff/load/close_transaction.cpp
    src/model/diff/load/devices.cpp
    src/model/diff/load/file_infos.cpp
    src/model/diff/load/folder_infos.cpp
    src/model/diff/load/folders.cpp
    src/model/diff/load/ignored_devices.cpp
    src/model/diff/load/ignored_folders.cpp
    src/model/diff/load/load_cluster.cpp
    src/model/diff/load/unknown_folders.cpp
    src/model/diff/modify/append_block.cpp
    src/model/diff/modify/blocks_availability.cpp
    src/model/diff/modify/clone_block.cpp
    src/model/diff/modify/clone_file.cpp
    src/model/diff/modify/connect_request.cpp
    src/model/diff/modify/create_folder.cpp
    src/model/diff/modify/file_availability.cpp
    src/model/diff/modify/finish_file.cpp
    src/model/diff/modify/flush_file.cpp
    src/model/diff/modify/lock_file.cpp
    src/model/diff/modify/local_update.cpp
    src/model/diff/modify/relay_connect_request.cpp
    src/model/diff/modify/share_folder.cpp
    src/model/diff/modify/unshare_folder.cpp
    src/model/diff/modify/update_contact.cpp
    src/model/diff/modify/update_peer.cpp
    src/model/diff/peer/peer_state.cpp
    src/model/diff/peer/cluster_remove.cpp
    src/model/diff/peer/cluster_update.cpp
    src/model/diff/peer/update_folder.cpp
    src/model/misc/block_iterator.cpp
    src/model/misc/error_code.cpp
    src/model/misc/file_block.cpp
    src/model/misc/file_iterator.cpp
    src/model/misc/updates_streamer.cpp
    src/model/misc/uuid.cpp
    src/model/misc/version_utils.cpp
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
    src/model/remote_folder_info.cpp
    src/model/unknown_folder.cpp
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
    src/utils/error_code.cpp
    src/utils/location.cpp
    src/utils/log.cpp
    src/utils/network_interface.cpp
    src/utils/platform.cpp
    src/utils/tls.cpp
    src/utils/uri.cpp
)

set(BUILD_SHARED_LIBS false CACHE BOOL "BUILD_SHARED_LIBS")
set(MDBX_BUILD_TOOLS false CACHE BOOL "MDBX_BUILD_TOOLS")
set(MDBX_ENABLE_TESTS false CACHE BOOL "MDBX_ENABLE_TESTS")
set(MDBX_BUILD_CXX false CACHE BOOL "MDBX_BUILD_CXX")

add_subdirectory("lib/mbdx")

target_include_directories(syncspirit_lib PUBLIC
    ${syncspirit_SOURCE_DIR}/lib/mbdx
    ${OPENSSL_INCLUDE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(syncspirit_lib
    syncspirit_pb
    rotor::asio
    rotor::thread
    pugixml::pugixml
    Boost::iostreams
    Boost::filesystem
    Boost::program_options
    Boost::system
    OpenSSL::SSL
    fmt::fmt
    spdlog::spdlog
    uriparser::uriparser
    tomlplusplus::tomlplusplus
    nlohmann_json::nlohmann_json
    lz4::lz4
    mdbx-static
    $<$<PLATFORM_ID:Windows>:iphlpapi>
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

enable_testing()
add_subdirectory("tests")
add_subdirectory("src/ui-daemon")
