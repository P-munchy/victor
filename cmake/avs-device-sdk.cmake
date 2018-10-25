
if(AVS_INCLUDED)
  return()
endif(AVS_INCLUDED)
set(AVS_INCLUDED true)

if (VICOS)
  set(LIBAVS_INCLUDE_PATH "${CORETECH_EXTERNAL_DIR}/build/avs-device-sdk/vicos/include")
  set(LIBAVS_LIB_PATH "${CORETECH_EXTERNAL_DIR}/build/avs-device-sdk/vicos/lib")
elseif (MACOSX)
  set(LIBAVS_INCLUDE_PATH "${CORETECH_EXTERNAL_DIR}/build/avs-device-sdk/mac/include")
  set(LIBAVS_LIB_PATH "${CORETECH_EXTERNAL_DIR}/build/avs-device-sdk/mac/lib")
endif()


set(AVS_LIBS
  ACL
  ADSL
  AFML
  AIP
  Alerts
  AudioPlayer
  AudioResources
  AVSCommon
  AVSSystem
  CapabilitiesDelegate
  CBLAuthDelegate
  CertifiedSender
  ContextManager
  ESP
  PlaybackController
  PlaylistParser
  RegistrationManager
  SpeakerManager
  SpeechSynthesizer
  SQLiteStorage
)

if (VICOS)
  set(AVS_LIB_EXT ".so")
elseif (MACOSX)
  set(AVS_LIB_EXT ".dylib")
endif()

foreach(LIB ${AVS_LIBS})
  add_library(${LIB} SHARED IMPORTED)
  set_target_properties(${LIB} PROPERTIES
    IMPORTED_LOCATION
    "${LIBAVS_LIB_PATH}/lib${LIB}${AVS_LIB_EXT}"
    INTERFACE_INCLUDE_DIRECTORIES
    "${LIBAVS_INCLUDE_PATH}")
  anki_build_target_license(${LIB} "Apache-2.0,${CMAKE_SOURCE_DIR}/licenses/avs-device-sdk.license" "MIT/X derivate,${CMAKE_SOURCE_DIR}/licenses/curl.license" "OpenSSL License/SSLeay License,${CMAKE_SOURCE_DIR}/licenses/openssl.license" "MIT License,${CMAKE_SOURCE_DIR}/licenses/nghttp2.license")
endforeach()

if (VICOS)
  # add SQLite 
  list(APPEND AVS_LIBS sqlite3)
  add_library(sqlite3 SHARED IMPORTED)
  set_target_properties(sqlite3 PROPERTIES
    IMPORTED_LOCATION
    "${LIBAVS_LIB_PATH}/libsqlite3.so.0"
    INTERFACE_INCLUDE_DIRECTORIES
    "${LIBAVS_INCLUDE_PATH}")
  anki_build_target_license(sqlite3 "Public domain,${CMAKE_SOURCE_DIR}/licenses/sqlite3.license")
endif()


#only needed for vicos
if (TARGET copy_avs_libs)
    return()
endif()

set(INSTALL_LIBS
  "${AVS_LIBS}")

message(STATUS "avs libs: ${INSTALL_LIBS}")

set(OUTPUT_FILES "")

foreach(lib ${INSTALL_LIBS})
    get_target_property(LIB_PATH ${lib} IMPORTED_LOCATION)
    get_filename_component(LIB_FILENAME ${LIB_PATH} NAME)
    set(DST_PATH "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${LIB_FILENAME}") 
    message(STATUS "copy avs lib: ${lib} ${LIB_PATH} -> ${DST_PATH}")
    add_custom_command(
        OUTPUT "${DST_PATH}"
        COMMAND ${CMAKE_COMMAND}
        ARGS -E copy_if_different "${LIB_PATH}" "${DST_PATH}"
        COMMENT "copy ${LIB_PATH}"
        VERBATIM
    )
    list(APPEND OUTPUT_FILES ${DST_PATH})
endforeach() 

add_custom_target(copy_avs_libs ALL DEPENDS ${OUTPUT_FILES})
