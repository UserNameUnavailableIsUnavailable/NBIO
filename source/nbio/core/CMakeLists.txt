cmake_minimum_required(VERSION 3.20)

target_sources(nbio PRIVATE epoll_multiplexer.cpp)

if(NBIO_ENABLE_IO_URING)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(URING_PC REQUIRED IMPORTED_TARGET liburing)
    target_sources(nbio PRIVATE uring_multiplexer.cpp)
    target_link_libraries(nbio PUBLIC PkgConfig::URING_PC)
endif()
