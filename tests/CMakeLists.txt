find_package(Catch2 3 CONFIG REQUIRED)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/test_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/test_config.h
)

function(basic_setup name)
    target_include_directories(
        ${name}
        PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/include
    )
    target_link_libraries(${name} PUBLIC subprocess)
endfunction()

function(add_catch_test target source)
    add_executable(${target} ${source})
    basic_setup(${target})
    target_link_libraries(${target} PRIVATE Catch2::Catch2WithMain)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

add_catch_test(basic_test basic_test.cpp)
add_executable(cat cat_child.cpp)
add_executable(echo echo_main.cpp)
add_executable(sleep sleep_main.cpp)
add_executable(printenv printenv_main.cpp)
add_executable(examples examples.cpp)

foreach(
    t
    cat
    echo
    sleep
    printenv
    examples
)
    basic_setup(${t})
endforeach()
