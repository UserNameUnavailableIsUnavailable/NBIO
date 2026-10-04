cmake_minimum_required(VERSION 3.20)

function(add_example name)
    add_executable(
        ${name}
        ${name}.cpp
    )
    target_link_libraries(
        ${name}
        PRIVATE
        nbio
    )
endfunction()

add_example(Echo)
add_example(Sleep)
add_example(Grace)
add_example(ScopedSignalService)

if(NBIO_ENABLE_IO_URING)
    add_example(Condition)
endif()

if(NBIO_ENABLE_RDMA)
endif()