cmake_minimum_required(VERSION 3.20)

find_package(CLI11 CONFIG REQUIRED)

function(add_benchmark target)
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE nbio CLI11::CLI11)
endfunction()

add_benchmark(rdma_deliver_benchmark)
