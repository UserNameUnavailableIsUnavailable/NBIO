cmake_minimum_required(VERSION 3.20)

add_executable(
	nbio_testing
	buffer_testing.cpp
	condition_testing.cpp
	file_write_channel_testing.cpp
	socket_channel_testing.cpp
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