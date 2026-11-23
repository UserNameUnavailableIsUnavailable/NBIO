cmake_minimum_required(VERSION 3.20)

add_executable(
	nbio_testing
	condition_testing.cpp
	file_write_channel_testing.cpp
	socket_channel_testing.cpp
	socket_testing.cpp
	address_testing.cpp
	scatter_testing.cpp
)

find_package(GTest REQUIRED)

target_link_libraries(
	nbio_testing
	PRIVATE
	nbio
	GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(nbio_testing)

if(WIN32)
    # This increment can be tested independently of the deferred Windows services.
    add_executable(nbio_windows_core_testing
        iocp_testing.cpp
        scatter_testing.cpp
        ../source/core/iocp_multiplexer.cpp
        ../source/async/coroutine.cpp
        ../source/async/scheduler.cpp
        ../source/time/timer.cpp
        ../source/time/timer_channel.cpp
        ../source/fs/file_read_channel.cpp
        ../source/fs/file_write_channel.cpp
        ../source/net/address.cpp
        ../source/net/socket.cpp
        ../source/net/tcp_connector.cpp
        ../source/net/tcp_receive_channel.cpp
        ../source/net/tcp_send_channel.cpp
    )
    target_include_directories(nbio_windows_core_testing PRIVATE ../include)
    target_compile_definitions(nbio_windows_core_testing PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(nbio_windows_core_testing PRIVATE tl::expected GTest::gtest_main ws2_32)
    gtest_discover_tests(nbio_windows_core_testing TEST_PREFIX windows_core.)
endif()