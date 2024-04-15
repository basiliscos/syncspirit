cmake_minimum_required(VERSION 3.9)

set(FLTK_ABI_VERSION "1.3.9")
set(FLTK_BUILD_TEST off)
set(OPTION_USE_XINERAMA off)
set(OPTION_BUILD_HTML_DOCUMENTATION off)
set(OPTION_BUILD_PDF_DOCUMENTATION off)
set(OPTION_USE_GL off)
set(OPTION_ABI_VERSION 10306)
add_subdirectory(${syncspirit_SOURCE_DIR}/lib/fltk fltk)

add_executable(syncspirit-fltk WIN32
    main.cpp
)

target_link_libraries(syncspirit-fltk
    syncspirit_lib
    fltk
    $<$<PLATFORM_ID:Windows>:ws2_32>
    $<$<PLATFORM_ID:Windows>:wsock32>
)
target_include_directories(syncspirit-fltk PUBLIC
    ${syncspirit_SOURCE_DIR}/src
    ${syncspirit_SOURCE_DIR}/lib/fltk/
    ${CMAKE_CURRENT_BINARY_DIR}/fltk/
)

if (("${CMAKE_BUILD_TYPE}" STREQUAL "Release") AND (NOT WIN32))
    include(CheckIPOSupported)
    check_ipo_supported(RESULT supported OUTPUT error)
    if( supported )
        message(STATUS "IPO / LTO enabled for the syncspirit-fltk")
        set_property(TARGET syncspirit-fltk PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "IPO / LTO not supported")
    endif()
endif()

if (CMAKE_BUILD_TYPE MATCHES "^([Rr]elease)|(MinSizeRel)")
    set_target_properties(syncspirit-fltk PROPERTIES LINK_FLAGS -s)
    find_program(UPX upx)
    if (NOT ${UPX} STREQUAL "UPX-NOTFOUND")
        message(STATUS "upx found")
        set(DAEMON_TARGET "${CMAKE_CXX_COMPILER}")
        string(REGEX REPLACE ".*/" "" DAEMON_TARGET ${DAEMON_TARGET})
        string(REGEX REPLACE "(.*)-.+" "\\1" DAEMON_TARGET ${DAEMON_TARGET})
        if ("${DAEMON_TARGET}" STREQUAL "")
            set(DAEMON_TARGET "unknown")
        endif()
        string(JOIN "_"  DAEMON_TARGET "syncspirit-fltk" ${SYNCSPIRIT_VERSION} ${DAEMON_TARGET})
        set(DAEMON_TARGET "${DAEMON_TARGET}${CMAKE_EXECUTABLE_SUFFIX}")

        set(EXE "syncspirit-fltk${CMAKE_EXECUTABLE_SUFFIX}")
        add_custom_target(compress_exec ALL
            COMMAND upx "--force" "-9" "-q" "-o" "${syncspirit_BINARY_DIR}/${DAEMON_TARGET}" ${EXE}
            DEPENDS ${EXE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "compressing ${DAEMON_TARGET}")
    else()
        message(WARNING "upx not found")
    endif()
endif()
