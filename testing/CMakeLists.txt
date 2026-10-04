cmake_minimum_required(VERSION 3.20)

add_executable(
	nbio_testing
	BufferTesting.cpp
	ConditionTesting.cpp
	FileWriteChannelTesting.cpp
	SocketChannelTesting.cpp
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