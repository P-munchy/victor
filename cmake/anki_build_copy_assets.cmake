function(anki_build_copy_assets)
    set(options "")
    set(oneValueArgs TARGET DEP_TARGET SRCLIST_DIR OUTPUT_DIR)
    set(multiValueArgs OUT_SRCS OUT_DSTS)
    cmake_parse_arguments(cpassets "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set(_SRCS "")
    set(_DSTS "")

    if (EXISTS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}.srcs.lst")
        file(STRINGS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}.srcs.lst" _SRCS)
    endif()

    if (EXISTS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}.dsts.lst")
        file(STRINGS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}.dsts.lst" _DSTS)
    endif()
    
    if (EXISTS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}_${ANKI_PLATFORM_NAME}.srcs.lst")
        file(STRINGS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}_${ANKI_PLATFORM_NAME}.srcs.lst" _PLATFORM_SRCS)
    endif()

    if (EXISTS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}_${ANKI_PLATFORM_NAME}.dsts.lst")
        file(STRINGS "${cpassets_SRCLIST_DIR}/${cpassets_TARGET}_${ANKI_PLATFORM_NAME}.dsts.lst" _PLATFORM_DSTS)
    endif()

    set(SRCS ${_SRCS} ${_PLATFORM_SRCS})
    set(DSTS ${_DSTS} ${_PLATFORM_DSTS})

    list(LENGTH SRCS SRCS_COUNT)
    math(EXPR SRCS_COUNT "${SRCS_COUNT} - 1")

    set(INPUT_FILES "")
    set(OUTPUT_FILES "")
    foreach(IDX RANGE ${SRCS_COUNT})
        list(GET SRCS ${IDX} SRC)
        list(GET DSTS ${IDX} DST)

        # uncomment for DEBUGGING
        # message(STATUS "cp ${SRC} ${DST}")

        set(SRC_PATH "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}")
        set(DST_PATH "${cpassets_OUTPUT_DIR}/${DST}")

        add_custom_command(
            OUTPUT ${DST_PATH}
            COMMAND ${CMAKE_COMMAND}
            ARGS -E copy_if_different "${SRC_PATH}" "${DST_PATH}"
            COMMENT "copy ${SRC} ${DST}"
            VERBATIM
        )
        list(APPEND OUTPUT_FILES ${DST_PATH})
        list(APPEND INPUT_FILES ${SRC_PATH})
    endforeach()

    set(${cpassets_OUT_DSTS} ${OUTPUT_FILES} PARENT_SCOPE)
    set(${cpassets_OUT_SRCS} ${INPUT_FILES} PARENT_SCOPE)

    if (NOT ${cpassets_DEP_TARGET})
        set(cpassets_DEP_TARGET ALL)
    endif()

    add_custom_target(${cpassets_TARGET} ALL DEPENDS ${OUTPUT_FILES})

endfunction(anki_build_copy_assets)
