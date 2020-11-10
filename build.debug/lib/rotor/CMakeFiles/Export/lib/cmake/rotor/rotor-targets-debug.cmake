#----------------------------------------------------------------
# Generated CMake target import file for configuration "Debug".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "rotor::rotor" for configuration "Debug"
set_property(TARGET rotor::rotor APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rotor::rotor PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librotor.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS rotor::rotor )
list(APPEND _IMPORT_CHECK_FILES_FOR_rotor::rotor "${_IMPORT_PREFIX}/lib/librotor.a" )

# Import target "rotor::rotor_asio" for configuration "Debug"
set_property(TARGET rotor::rotor_asio APPEND PROPERTY IMPORTED_CONFIGURATIONS DEBUG)
set_target_properties(rotor::rotor_asio PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_DEBUG "CXX"
  IMPORTED_LOCATION_DEBUG "${_IMPORT_PREFIX}/lib/librotor_asio.a"
  )

list(APPEND _IMPORT_CHECK_TARGETS rotor::rotor_asio )
list(APPEND _IMPORT_CHECK_FILES_FOR_rotor::rotor_asio "${_IMPORT_PREFIX}/lib/librotor_asio.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
