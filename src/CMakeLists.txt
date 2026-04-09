file(GLOB_RECURSE srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(subprocess STATIC)
target_sources(subprocess PUBLIC FILE_SET CXX_MODULES FILES ${srcs})
target_include_directories(subprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
include(GNUInstallDirs)
install(
    TARGETS subprocess
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    CXX_MODULES_BMI DESTINATION ${CMAKE_INSTALL_LIBDIR}/cxx/bmi
)
configure_file(${CMAKE_SOURCE_DIR}/packaging/subprocess.pc.in subprocess.pc @ONLY)
install(
    FILES ${CMAKE_CURRENT_BINARY_DIR}/subprocess.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
