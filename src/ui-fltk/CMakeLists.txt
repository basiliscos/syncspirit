cmake_minimum_required(VERSION 3.9)

find_package(libqrencode REQUIRED)

add_executable(syncspirit-fltk WIN32
    app_supervisor.cpp
    augmentation.cpp
    content.cpp
    log_colors.cpp
    log_panel.cpp
    log_sink.cpp
    log_table.cpp
    log_utils.cpp
    main.cpp
    main_window.cpp
    qr_button.cpp
    static_table.cpp
    tree_item.cpp
    tree_view.cpp
    config/category.cpp
    config/control.cpp
    config/properties.cpp
    config/property.cpp
    config/table.cpp
    content/folder_table.cpp
    table_widget/checkbox.cpp
    table_widget/choice.cpp
    table_widget/input.cpp
    table_widget/int_input.cpp
    table_widget/label.cpp
    table_widget/path.cpp
    tree_item/devices.cpp
    tree_item/folder.cpp
    tree_item/folders.cpp
    tree_item/ignored_device.cpp
    tree_item/ignored_devices.cpp
    tree_item/peer_device.cpp
    tree_item/peer_dir_base.cpp
    tree_item/peer_folder.cpp
    tree_item/peer_folders.cpp
    tree_item/pending_device.cpp
    tree_item/pending_devices.cpp
    tree_item/pending_folder.cpp
    tree_item/pending_folders.cpp
    tree_item/self_device.cpp
    tree_item/settings.cpp
    tree_item/virtual_dir.cpp
)

target_link_libraries(syncspirit-fltk
    syncspirit_lib
    rotor::fltk
    $<$<PLATFORM_ID:Linux>:jpeg>
    $<$<PLATFORM_ID:Linux>:png>
    libqrencode::libqrencode
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
        add_custom_target(compress-syncspirit-fltk ALL
            COMMAND upx "--force" "-9" "-q" "-o" "${syncspirit_BINARY_DIR}/${DAEMON_TARGET}" ${EXE}
            DEPENDS ${EXE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "compressing ${DAEMON_TARGET}")
    else()
        message(WARNING "upx not found")
    endif()
endif()
