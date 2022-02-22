cmake_minimum_required(VERSION 3.9)

add_executable(syncspirit-daemon
    command/add_folder.cpp
    command/add_peer.cpp
    command/inactivate.cpp
    command/pair_iterator.cpp
    command/share_folder.cpp
    command.cpp
    error_code.cpp
    governor_actor.cpp
    main.cpp
)

target_link_libraries(syncspirit-daemon
    syncspirit_lib
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:wsock32>
)
target_include_directories(syncspirit-daemon PUBLIC ${syncspirit_SOURCE_DIR}/src)

message(STATUS "build type ${CMAKE_BUILD_TYPE}")

#if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
#    include(CheckIPOSupported)
#    check_ipo_supported(RESULT supported OUTPUT error)
#    if( supported )
#        message(STATUS "IPO / LTO enabled for the syncspirit-daemon")
#        set_property(TARGET syncspirit-daemon PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
#    else()
#        message(STATUS "IPO / LTO not supported: <${error}>")
#    endif()
#endif()
