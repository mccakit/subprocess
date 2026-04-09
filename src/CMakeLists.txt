file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(subprocess STATIC)
target_sources(subprocess PUBLIC FILE_SET CXX_MODULES FILES ${srcs})
target_include_directories(subprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
