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

if (("${CMAKE_BUILD_TYPE}" STREQUAL "Release") AND (NOT WIN32))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT error)
    if( supported )
        message(STATUS "IPO / LTO enabled for the syncspirit-daemon")
        set_property(TARGET syncspirit-daemon PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO / LTO not supported: <${error}>")
    endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES "^([Rr]elease)|(MinSizeRel)")
    set_target_properties(syncspirit-daemon PROPERTIES LINK_FLAGS -s)
    find_program(UPX upx)
    if (NOT ${UPX} STREQUAL "UPX-NOTFOUND")
        message(STATUS "upx found")
        set(DAEMON_TARGET "${CMAKE_CXX_COMPILER}")
        string(REGEX REPLACE ".*/" "" DAEMON_TARGET ${DAEMON_TARGET})
        string(REGEX REPLACE "(.*)-.+" "\\1" DAEMON_TARGET ${DAEMON_TARGET})
        if ("${DAEMON_TARGET}" STREQUAL "")
            set(DAEMON_TARGET "unknown")
        endif()
        string(JOIN "_"  DAEMON_TARGET "syncspirit-daemon" ${SYNCSPIRIT_VERSION} ${DAEMON_TARGET})
        set(DAEMON_TARGET "${DAEMON_TARGET}${CMAKE_EXECUTABLE_SUFFIX}")

        set(EXE "syncspirit-daemon${CMAKE_EXECUTABLE_SUFFIX}")
        add_custom_target(compress_exec ALL
            COMMAND upx "--force" "-9" "-q" "-o" "${syncspirit_BINARY_DIR}/${DAEMON_TARGET}" ${EXE}
            DEPENDS ${EXE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "compressing ${DAEMON_TARGET}")
    else()
        message(WARNING "upx not found")
    endif()
endif()
