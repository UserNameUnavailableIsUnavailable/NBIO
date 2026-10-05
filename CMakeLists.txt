cmake_minimum_required(VERSION 3.20)

if(BUILD_EXAMPLES AND NOT "examples" IN_LIST VCPKG_MANIFEST_FEATURES)
    list(APPEND VCPKG_MANIFEST_FEATURES examples)
endif()

if(BUILD_TESTING AND NOT "testing" IN_LIST VCPKG_MANIFEST_FEATURES)
    list(APPEND VCPKG_MANIFEST_FEATURES testing)
endif()

if(BUILD_BENCHMARKS AND NOT "benchmark" IN_LIST VCPKG_MANIFEST_FEATURES)
    list(APPEND VCPKG_MANIFEST_FEATURES benchmark)
endif()

project(nbio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(NBIO_IO_URING_DEFAULT OFF)
set(NBIO_RDMA_DEFAULT OFF)
set(NBIO_MEMORY_POOLING_DEFAULT OFF)
set(NBIO_TESTING_DEFAULT OFF)
set(NBIO_BENCHMARK_DEFAULT OFF)
if("io-uring" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_IO_URING_DEFAULT ON)
endif()
if("rdma" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_RDMA_DEFAULT ON)
endif()
if("custom-memory-pooling" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_MEMORY_POOLING_DEFAULT ON)
endif()
if("testing" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_TESTING_DEFAULT ON)
endif()
if("benchmark" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_BENCHMARK_DEFAULT ON)
endif()

option(NBIO_ENABLE_IO_URING "Enable the Linux io_uring multiplexer" ${NBIO_IO_URING_DEFAULT})
option(NBIO_ENABLE_RDMA "Enable Linux RDMA support" ${NBIO_RDMA_DEFAULT})
option(NBIO_ENABLE_CUSTOM_MEMORY_POOLING "Enable NBIO's Linux global allocation pool" ${NBIO_MEMORY_POOLING_DEFAULT})
option(NBIO_BUILD_EXAMPLES "Build NBIO examples" OFF)
option(BUILD_TESTING "Build NBIO tests" ${NBIO_TESTING_DEFAULT})
option(BUILD_BENCHMARKS "Build NBIO benchmarks" ${NBIO_BENCHMARK_DEFAULT})

include(CTest)
include(GNUInstallDirs)

if((NBIO_ENABLE_IO_URING OR NBIO_ENABLE_RDMA) AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "io_uring and RDMA support are available only on Linux")
endif()

add_subdirectory(source/nbio)

find_package(tl-expected CONFIG REQUIRED)
target_link_libraries(nbio PUBLIC tl::expected)

target_include_directories(nbio PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(nbio PUBLIC cxx_std_20)

if(NBIO_ENABLE_IO_URING)
    message(STATUS "io_uring enabled")
    target_compile_definitions(nbio PUBLIC NBIO_ENABLE_IO_URING=1)
endif()

if(NBIO_ENABLE_RDMA)
    message(STATUS "rdma enabled")
    target_compile_definitions(nbio PUBLIC NBIO_ENABLE_RDMA=1)
endif()

if(NBIO_ENABLE_CUSTOM_MEMORY_POOLING)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "Custom memory pooling is currently available only on Linux")
    endif()
    target_compile_definitions(nbio PUBLIC CUSTOM_MEMORY_POOLING=1)
    target_link_options(nbio INTERFACE
        "-Wl,--wrap=malloc"
        "-Wl,--wrap=calloc"
        "-Wl,--wrap=realloc"
        "-Wl,--wrap=free")
endif()

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

if(BUILD_TESTING)
    add_subdirectory(testing)
endif()

include(CMakePackageConfigHelpers)

set(NBIO_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/nbio")
configure_package_config_file(
    cmake/nbioConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/nbioConfig.cmake"
    INSTALL_DESTINATION "${NBIO_INSTALL_CMAKEDIR}"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/nbioConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

install(TARGETS nbio EXPORT nbioTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(DIRECTORY include/nbio DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT nbioTargets
    FILE nbioTargets.cmake
    NAMESPACE nbio::
    DESTINATION "${NBIO_INSTALL_CMAKEDIR}"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/nbioConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/nbioConfigVersion.cmake"
    DESTINATION "${NBIO_INSTALL_CMAKEDIR}"
)