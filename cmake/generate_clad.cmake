# helper functions for generating code from clad


# constructs custom commands for executing a specified clad emiiter
# on inputs to generate outpts (in OUTPUT_SRCS).  If LIBRARY is specified,
# creates a library target that depends on the output of the clad emitter
# commands
function(generate_clad)
    set(options "")
    set(oneValueArgs LIBRARY RELATIVE_SRC_DIR OUTPUT_FILE OUTPUT_DIR EMITTER)
    set(multiValueArgs SRCS OUTPUT_EXTS INCLUDES FLAGS OUTPUT_SRCS)
    cmake_parse_arguments(genclad "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # message(STATUS "LIBRARY ${genclad_LIBRARY}")

    if(NOT genclad_OUTPUT_EXTS)
        set(genclad_OUTPUT_EXTS ".h" ".cpp")
    endif()

    set(OUTPUT_FILES "")

    foreach(CLAD_SRC ${genclad_SRCS})
        get_filename_component(SRC_DIR ${CLAD_SRC} DIRECTORY)
        get_filename_component(SRC_BASENAME ${CLAD_SRC} NAME_WE)

        set(SRC_DIR_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${genclad_RELATIVE_SRC_DIR})
        get_filename_component(CLAD_SRC_ABS ${CMAKE_CURRENT_SOURCE_DIR}/${CLAD_SRC} ABSOLUTE)
        file(RELATIVE_PATH CLAD_SRC_REL ${SRC_DIR_ABS} ${CLAD_SRC_ABS})

        #message(STATUS "CLAD_SRC: ${CLAD_SRC}")
        #message(STATUS "  SRC_DIR: ${SRC_DIR}")
        #message(STATUS "  SRC_BASENAME: ${SRC_BASENAME}")
        #message(STATUS "  ABS: ${CLAD_SRC_ABS}")
        #message(STATUS "  REL: ${CLAD_SRC_REL}")

        get_filename_component(REL_OUTPUT_DIR ${CLAD_SRC_REL} DIRECTORY)

        set(OUTPUT_BASE "${genclad_OUTPUT_DIR}/${REL_OUTPUT_DIR}/${SRC_BASENAME}")

        set(CLAD_EMITTER_OUTPUTS "")
        foreach(OUTPUT_EXT ${genclad_OUTPUT_EXTS})
            list(APPEND CLAD_EMITTER_OUTPUTS "${OUTPUT_BASE}${OUTPUT_EXT}")
        endforeach()

        set(INCLUDES "")
        if (genclad_INCLUDES)
            list(APPEND INCLUDES "-I")
            list(APPEND INCLUDES ${genclad_INCLUDES})
        endif()

        set(INPUT_DIR "")
        if (genclad_RELATIVE_SRC_DIR)
            set(INPUT_DIR "-C" "${genclad_RELATIVE_SRC_DIR}")
        endif()

        set(OUTPUT_OPTION "-o" "${genclad_OUTPUT_DIR}")
        if (genclad_OUTPUT_FILE)
            set(OUTPUT_OPTION "-o" ${CLAD_EMITTER_OUTPUTS})
            # message(STATUS "OUTPUT_OPTION ${OUTPUT_OPTION}")
        endif()

        #message(STATUS "INCLUDES: ${INCLUDES}")
        #message(STATUS "CLAD_EMITTER_OUTPUTS: ${CLAD_EMITTER_OUTPUTS}")

        add_custom_command(
            COMMAND /usr/bin/env python ${genclad_EMITTER} ${genclad_FLAGS} ${INPUT_DIR} ${INCLUDES} ${OUTPUT_OPTION} ${CLAD_SRC_REL}
            DEPENDS ${genclad_EMITTER} ${CLAD_SRC}
            OUTPUT ${CLAD_EMITTER_OUTPUTS}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Generating code for ${CLAD_SRC}"
        )

        list(APPEND OUTPUT_FILES ${CLAD_EMITTER_OUTPUTS})
    endforeach()

    set(${genclad_OUTPUT_SRCS} ${OUTPUT_FILES} PARENT_SCOPE)

    if (genclad_LIBRARY)
        add_library(${genclad_LIBRARY} STATIC
            ${OUTPUT_FILES}
        )
    endif()

endfunction()

function(generate_clad_cpplite)
    set(options "")
    set(oneValueArgs LIBRARY RELATIVE_SRC_DIR OUTPUT_DIR)
    set(multiValueArgs SRCS INCLUDES FLAGS OUTPUT_SRCS)
    cmake_parse_arguments(genclad "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(CLAD_BASE_DIR "${CMAKE_SOURCE_DIR}/tools/message-buffers")
    set(CLAD_EMITTER_DIR "${CLAD_BASE_DIR}/emitters")

    set(CLAD_CPP "${CLAD_EMITTER_DIR}/CPPLite_emitter.py")
    set(CLAD_CPP_EXTS ".h" ".cpp")
    set(CLAD_CPP_FLAGS
        "--max-message-size" "1400"
        "${genclad_FLAGS}")

    set(CLAD_CPP_DECL "${CMAKE_SOURCE_DIR}/robot/clad/cozmo_CPP_declarations_emitter.py")
    set(CLAD_CPP_DECL_EXTS "_declarations.def")
    set(CLAD_CPP_DECL_FLAGS "")

    set(CLAD_CPP_SWITCH "${CMAKE_SOURCE_DIR}/robot/clad/cozmo_CPPLite_switch_emitter.py")
    set(CLAD_CPP_SWITCH_EXTS "_switch.def")
    set(CLAD_CPP_SWITCH_FLAGS "")

    set(CLAD_CPP_SEND_HELPER "${CMAKE_SOURCE_DIR}/robot/clad/cozmo_CPPLite_send_helper_emitter.py")
    set(CLAD_CPP_SEND_HELPER_EXTS "_send_helper.def")
    set(CLAD_CPP_SEND_HELPER_FLAGS "")

    set(CLAD_HASH "${CLAD_EMITTER_DIR}/ASTHash_emitter.py")
    set(CLAD_HASH_EXTS "_hash.h")
    set(CLAD_HASH_FLAGS "")

    set(EMITTERS ${CLAD_CPP} ${CLAD_CPP_DECL} ${CLAD_CPP_SWITCH} ${CLAD_CPP_SEND_HELPER})
    set(EXTS CLAD_CPP_EXTS CLAD_CPP_DECL_EXTS CLAD_CPP_SWITCH_EXTS CLAD_CPP_SEND_HELPER_EXTS)
    set(FLAGS CLAD_CPP_FLAGS CLAD_CPP_DECL_FLAGS CLAD_CPP_SWITCH_FLAGS CLAD_CPP_SEND_HELPER_FLAGS)
    list(LENGTH EMITTERS EMITTER_COUNT)
    math(EXPR EMITTER_COUNT "${EMITTER_COUNT} - 1")

    set(CLAD_GEN_OUTPUTS "")

    #message(STATUS "EMITTERS : ${EMITTERS}")
    #message(STATUS "    EXTS : ${EXTS}")
    #message(STATUS "     LEN : ${EMITTER_COUNT}")

    foreach(IDX RANGE ${EMITTER_COUNT})
        list(GET EMITTERS ${IDX} EMITTER)

        list(GET EXTS ${IDX} EXT_LIST_SYM)
        set(EMITTER_EXTS "${${EXT_LIST_SYM}}")

        if (FLAGS)
            list(GET FLAGS ${IDX} FLAG_LIST_SYM)
            set(EMITTER_FLAGS "${${FLAG_LIST_SYM}}")
        else()
            set(EMITTER_FLAGS "")
        endif()

        # message(STATUS "${IDX} :: ${EMITTER} -- ${EMITTER_EXTS} -- ${EMITTER_FLAGS}")

        generate_clad(
            SRCS ${genclad_SRCS}
            RELATIVE_SRC_DIR ${genclad_RELATIVE_SRC_DIR}
            OUTPUT_DIR ${genclad_OUTPUT_DIR}
            EMITTER ${EMITTER}
            OUTPUT_EXTS ${EMITTER_EXTS}
            FLAGS ${EMITTER_FLAGS}
            INCLUDES ${genclad_INCLUDES}
            OUTPUT_SRCS CLAD_GEN_SRCS
        )

        #message(STATUS "CLAD_GEN_SRCS :: ${CLAD_GEN_SRCS}")
        list(APPEND CLAD_GEN_OUTPUTS ${CLAD_GEN_SRCS})
    endforeach()

    set(${genclad_OUTPUT_SRCS} ${CLAD_GEN_OUTPUTS} PARENT_SCOPE)

    if (genclad_LIBRARY)
        add_library(${genclad_LIBRARY} STATIC
            ${CLAD_GEN_OUTPUTS}
        )
    endif()
endfunction()

# calls generate_clad using each of the C++ emitters used for cozmoEngine
function(generate_clad_cpp)
    set(options "")
    set(oneValueArgs LIBRARY RELATIVE_SRC_DIR OUTPUT_DIR)
    set(multiValueArgs SRCS INCLUDES FLAGS OUTPUT_SRCS)
    cmake_parse_arguments(genclad "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    set(CLAD_BASE_DIR "${CMAKE_SOURCE_DIR}/tools/message-buffers")
    set(CLAD_EMITTER_DIR "${CLAD_BASE_DIR}/emitters")

    set(CLAD_CPP "${CLAD_EMITTER_DIR}/CPP_emitter.py")
    set(CLAD_CPP_EXTS ".h" ".cpp")
    set(CLAD_CPP_FLAGS
        "--output-union-helper-constructors"
        "--output-json"
        "${genclad_FLAGS}")

    set(CLAD_CPP_DECL "${CMAKE_SOURCE_DIR}/robot/clad/cozmo_CPP_declarations_emitter.py")
    set(CLAD_CPP_DECL_EXTS "_declarations.def")
    set(CLAD_CPP_DECL_FLAGS "")

    set(CLAD_CPP_SWITCH "${CMAKE_SOURCE_DIR}/clad/cozmo_CPP_switch_emitter.py")
    set(CLAD_CPP_SWITCH_EXTS "_switch.def")
    set(CLAD_CPP_SWITCH_FLAGS "")

    set(CLAD_HASH "${CLAD_EMITTER_DIR}/ASTHash_emitter.py")
    set(CLAD_HASH_EXTS "_hash.h")
    set(CLAD_HASH_FLAGS "")

    set(EMITTERS ${CLAD_CPP} ${CLAD_CPP_DECL} ${CLAD_CPP_SWITCH})
    set(EXTS CLAD_CPP_EXTS CLAD_CPP_DECL_EXTS CLAD_CPP_SWITCH_EXTS)
    set(FLAGS CLAD_CPP_FLAGS CLAD_CPP_DECL_FLAGS CLAD_CPP_SWITCH_FLAGS)
    list(LENGTH EMITTERS EMITTER_COUNT)
    math(EXPR EMITTER_COUNT "${EMITTER_COUNT} - 1")

    set(CLAD_GEN_OUTPUTS "")

    #message(STATUS "EMITTERS : ${EMITTERS}")
    #message(STATUS "    EXTS : ${EXTS}")
    #message(STATUS "     LEN : ${EMITTER_COUNT}")

    foreach(IDX RANGE ${EMITTER_COUNT})
        list(GET EMITTERS ${IDX} EMITTER)

        list(GET EXTS ${IDX} EXT_LIST_SYM)
        set(EMITTER_EXTS "${${EXT_LIST_SYM}}")

        if (FLAGS)
            list(GET FLAGS ${IDX} FLAG_LIST_SYM)
            set(EMITTER_FLAGS "${${FLAG_LIST_SYM}}")
        else()
            set(EMITTER_FLAGS "")
        endif()

        # message(STATUS "${IDX} :: ${EMITTER} -- ${EMITTER_EXTS} -- ${EMITTER_FLAGS}")

        generate_clad(
            SRCS ${genclad_SRCS}
            RELATIVE_SRC_DIR ${genclad_RELATIVE_SRC_DIR}
            OUTPUT_DIR ${genclad_OUTPUT_DIR}
            EMITTER ${EMITTER}
            OUTPUT_EXTS ${EMITTER_EXTS}
            FLAGS ${EMITTER_FLAGS}
            INCLUDES ${genclad_INCLUDES}
            OUTPUT_SRCS CLAD_GEN_SRCS
        )

        #message(STATUS "CLAD_GEN_SRCS :: ${CLAD_GEN_SRCS}")
        list(APPEND CLAD_GEN_OUTPUTS ${CLAD_GEN_SRCS})
    endforeach()

    set(${genclad_OUTPUT_SRCS} ${CLAD_GEN_OUTPUTS} PARENT_SCOPE)

    if (genclad_LIBRARY)
        add_library(${genclad_LIBRARY} STATIC
            ${CLAD_GEN_OUTPUTS}
        )
    endif()

endfunction()
