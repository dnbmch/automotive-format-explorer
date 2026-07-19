find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

# On Windows the test executables resolve explorer-core / the backend DLLs from
# their own directory, but Qt's DLLs live in the Qt install. Hand ctest a PATH
# that includes them so the suite runs without developer shell setup.
if(WIN32)
    set(_test_path "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS};$ENV{PATH}")
    string(REPLACE "/" "\\" _test_path "${_test_path}")
    string(REPLACE ";" "\\;" _test_path "${_test_path}")
endif()

# Test executables are emitted next to the libraries they link so the shared
# explorer-core / backend binaries resolve at runtime.
function(explorer_add_test name)
    add_executable(${name} ${name}.cpp)
    target_include_directories(${name} PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    set_target_properties(${name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        WIN32_EXECUTABLE FALSE
    )
    add_test(NAME ${name} COMMAND ${name})
    if(WIN32)
        set_tests_properties(${name} PROPERTIES ENVIRONMENT "PATH=${_test_path}")
    endif()
endfunction()

explorer_add_test(tst_treefiltermodel explorer-core)

explorer_add_test(tst_a2ldetailpresenter
    explorer-core
    explorer-a2l-backend
    protobuf::libprotobuf
    a2lparser
)
