cmake_minimum_required(VERSION 4.2)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
set(CMAKE_CXX_MODULE_STD ON)
project(subprocess VERSION 0.0.0 DESCRIPTION "placeholder" LANGUAGES C CXX)

set(_CONAN_HOST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/conan/full_deploy/host")
file(GLOB _CONAN_PACKAGES LIST_DIRECTORIES true "${_CONAN_HOST_DIR}/*/*/*/*")
list(APPEND CMAKE_PREFIX_PATH ${_CONAN_PACKAGES})
file(
    GLOB _CONAN_PACKAGES_PKG
    LIST_DIRECTORIES true
    "${_CONAN_HOST_DIR}/*/*/*/*/lib/pkgconfig"
)
set(ENV{PKG_CONFIG_LIBDIR} ${_CONAN_PACKAGES_PKG})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif()
