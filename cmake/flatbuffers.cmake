set(FLATBUFFERS_INCLUDE_PATH "${CORETECH_EXTERNAL_DIR}/flatbuffers/include")

set(FLATBUFFERS_LIBS
  flatbuffers
)

if (ANDROID)
  set(FLATBUFFERS_LIB_PATH "${CORETECH_EXTERNAL_DIR}/flatbuffers/android/armeabi-v7a")
  list(APPEND FLATBUFFERS_LIBS flatbuffers_extra)
elseif (MACOSX)
  set(FLATBUFFERS_LIB_PATH "${CORETECH_EXTERNAL_DIR}/flatbuffers/ios/Release")
endif()

foreach(LIB ${FLATBUFFERS_LIBS})
  add_library(${LIB} STATIC IMPORTED)
  set_target_properties(${LIB} PROPERTIES
    IMPORTED_LOCATION
    "${FLATBUFFERS_LIB_PATH}/lib${LIB}.a"
    INTERFACE_INCLUDE_DIRECTORIES
    "${FLATBUFFERS_INCLUDE_PATH}")
endforeach()
