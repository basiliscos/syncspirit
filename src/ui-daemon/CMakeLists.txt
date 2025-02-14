cmake_minimum_required (VERSION 3.19)

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
    syncspirit_core
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:wsock32>
)
target_include_directories(syncspirit-daemon PUBLIC ${syncspirit_SOURCE_DIR}/src)
install_executable("syncspirit-daemon")

if (("${CMAKE_BUILD_TYPE}" STREQUAL "Release") AND (NOT WIN32))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT error)
    if( supported )
        message(STATUS "IPO / LTO enabled for the syncspirit-daemon")
        set_property(TARGET syncspirit-daemon PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO / LTO not supported")
    endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES "^([Rr]elease)|(MinSizeRel)")
    set_target_properties(syncspirit-daemon PROPERTIES LINK_FLAGS -s)
endif()
