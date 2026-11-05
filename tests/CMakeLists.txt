include_directories(BEFORE main)
set(UtilsHeader "")
file(REAL_PATH "main/nv_test_utils.h" UtilsHeader)
add_compile_definitions(USR_UTILS_HEADER="${UtilsHeader}")
add_library(nv_test
    OBJECT
    main/nv_test.c
    ../src/clib_mem.c
    ../src/clib_threads.c # needed for clib_mem.c
)

function(add_tests_in_directory TestsNamespace)
    file(GLOB TestSources "*.c")
    set(ExpandedSources "")
    list(POP_FRONT ARGV)
    foreach(src IN LISTS ARGV)
        file(REAL_PATH "../../src/${src}" ExpandedSource)
        list(APPEND ExpandedSources ${ExpandedSource})
    endforeach()

    add_library(${TestsNamespace} OBJECT ${ExpandedSources})

    foreach(TestSource ${TestSources})
        string(REPLACE ".c" "" TestName ${TestSource})
        string(REGEX REPLACE ".*/" "" TestName ${TestName})

        add_executable(
            ${TestName}
            ${TestSource}
        )
        target_link_libraries(${TestName} nv_test ${TestsNamespace})

        add_test(${TestName} ${TestName})
    endforeach()
endfunction()

# add_subdirectory(test_dummy)
add_subdirectory(test_nv_array)
add_subdirectory(test_nv_binds)
add_subdirectory(test_nv_buffer)
add_subdirectory(test_nv_context)
add_subdirectory(test_nv_pool)
add_subdirectory(test_nv_string)
add_subdirectory(test_nv_unicode)
