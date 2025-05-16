cmake_minimum_required (VERSION 3.19)

find_package(libqrencode REQUIRED)

if(MINGW)
    enable_language(RC)
endif()

add_executable(syncspirit-fltk WIN32 MACOSX_BUNDLE
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
    presence_item.cpp
    qr_button.cpp
    static_table.cpp
    symbols.cpp
    toolbar.cpp
    tree_item.cpp
    tree_view.cpp
    utils.cpp
    config/category.cpp
    config/control.cpp
    config/properties.cpp
    config/property.cpp
    config/table.cpp
    content/file_table.cpp
    content/folder_table.cpp
    presence_item/cluster.cpp
    presence_item/file.cpp
    presence_item/folder.cpp
    presence_item/missing.cpp
    table_widget/checkbox.cpp
    table_widget/choice.cpp
    table_widget/input.cpp
    table_widget/int_input.cpp
    table_widget/label.cpp
    table_widget/path.cpp
    tree_item/devices.cpp
    tree_item/folders.cpp
    tree_item/ignored_device.cpp
    tree_item/ignored_devices.cpp
    tree_item/peer_device.cpp
    tree_item/peer_folders.cpp
    tree_item/pending_device.cpp
    tree_item/pending_devices.cpp
    tree_item/pending_folder.cpp
    tree_item/pending_folders.cpp
    tree_item/self_device.cpp
    tree_item/settings.cpp
    $<$<PLATFORM_ID:Windows>:app.rc>
    $<$<PLATFORM_ID:Darwin>:${syncspirit_SOURCE_DIR}/misc/SyncSpiritIcon.icns>
)

target_link_libraries(syncspirit-fltk
    syncspirit_core
    rotor::fltk
    $<$<PLATFORM_ID:Linux>:jpeg>
    $<$<PLATFORM_ID:Linux>:png>
    libqrencode::libqrencode
)
target_include_directories(syncspirit-fltk PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${CMAKE_CURRENT_BINARY_DIR}/fltk
    ${syncspirit_SOURCE_DIR}/misc
    ${syncspirit_SOURCE_DIR}/src
    ${syncspirit_SOURCE_DIR}/lib/fltk
)

install_executable("syncspirit-fltk")
#install(TARGETS "syncspirit-fltk")

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
endif()

set_target_properties(syncspirit-fltk PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME    syncspirit
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.syncspirit-fltk"
    MACOSX_BUNDLE_BUNDLE_VERSION "${CMAKE_PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${CMAKE_PROJECT_VERSION}"
    MACOSX_BUNDLE_ICON_FILE "SyncSpiritIcon.icns"
    XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY TRUE
)
set_source_files_properties(${syncspirit_SOURCE_DIR}/misc/SyncSpiritIcon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
