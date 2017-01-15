include(${CMAKE_SOURCE_DIR}/cmake/FindJuliaInterp.cmake)

if(NOT JULIAINTERP_FOUND)
    message(STATUS "Disabling Julia bindings")
    set(BUILD_JULIA OFF)
    return()
endif()

set(JULIA_SOURCES Segyio.jl)

function(add_julia_package PACKAGE_NAME PACKAGE_PATH JULIA_FILES)
    add_custom_target(package_${PACKAGE_NAME} ALL)

    foreach(file ${JULIA_FILES})
        add_custom_command(
            TARGET package_${PACKAGE_NAME}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/julia/${PACKAGE_PATH}
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${CMAKE_BINARY_DIR}/julia/${PACKAGE_PATH}
            )
    endforeach()

    set_target_properties(
        package_${PACKAGE_NAME} PROPERTIES
        PACKAGE_INSTALL_PATH ${CMAKE_INSTALL_PREFIX}/${JULIA_INSTALL_PREFIX}/${PACKAGE_PATH}
        )
    install(FILES ${JULIA_FILES}
            DESTINATION ${CMAKE_INSTALL_PREFIX}/${JULIA_INSTALL_PREFIX}/${PACKAGE_PATH})
endfunction()

function(add_julia_test TESTNAME JULIA_TEST_FILE)
    configure_file(${JULIA_TEST_FILE} ${JULIA_TEST_FILE} COPYONLY)

    add_test(NAME julia.${TESTNAME}
             COMMAND ${JULIA_EXECUTABLE} ${JULIA_TEST_FILE}
            )

    to_path_list(juliapath "${CMAKE_BINARY_DIR}/julia/Segyio" "$ENV{JULIA_LOAD_PATH}")
    to_path_list(segypath  "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}" "$ENV{PATH}")

    set_property(TEST julia.${TESTNAME}
                 PROPERTY
                 ENVIRONMENT "JULIA_LOAD_PATH=${juliapath}"
                             "PATH=${segypath}"
                 )
endfunction()

configure_file(${testdata}/small.sgy    test-data/small.sgy    COPYONLY)
configure_file(${testdata}/small-ps.sgy test-data/small-ps.sgy COPYONLY)
add_julia_package(Segyio Segyio "${JULIA_SOURCES}")
add_julia_test(segy test/test_segy.jl)
