cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "vcpkg toolchain file")

project(NBIO)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(NBIO_IO_URING_DEFAULT OFF)
set(NBIO_RDMA_DEFAULT OFF)
set(NBIO_TESTING_DEFAULT OFF)
set(NBIO_BENCHMARK_DEFAULT OFF)
if("io-uring" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_IO_URING_DEFAULT ON)
endif()
if("rdma" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_RDMA_DEFAULT ON)
endif()
if("testing" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_TESTING_DEFAULT ON)
endif()
if("benchmark" IN_LIST VCPKG_MANIFEST_FEATURES)
    set(NBIO_BENCHMARK_DEFAULT ON)
endif()

option(NBIO_ENABLE_IO_URING "Enable the Linux io_uring multiplexer" ${NBIO_IO_URING_DEFAULT})
option(NBIO_ENABLE_RDMA "Enable Linux RDMA support" ${NBIO_RDMA_DEFAULT})
option(NBIO_BUILD_EXAMPLES "Build NBIO examples" OFF)
option(BUILD_TESTING "Build NBIO tests" ${NBIO_TESTING_DEFAULT})
option(BUILD_BENCHMARKS "Build NBIO benchmarks" ${NBIO_BENCHMARK_DEFAULT})

include(CTest)

if((NBIO_ENABLE_IO_URING OR NBIO_ENABLE_RDMA) AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "io_uring and RDMA support are available only on Linux")
endif()

add_subdirectory(source)

find_package(tl-expected CONFIG REQUIRED)
target_link_libraries(nbio INTERFACE tl::expected)

target_include_directories(nbio INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(NBIO_ENABLE_IO_URING)
    target_compile_definitions(nbio INTERFACE NBIO_ENABLE_IO_URING=1)
endif()

if(NBIO_ENABLE_RDMA)
    target_compile_definitions(nbio INTERFACE NBIO_ENABLE_RDMA=1)
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