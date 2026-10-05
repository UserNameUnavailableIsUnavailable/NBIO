cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    file.cpp
    file_view.cpp
    file_read_channel.cpp
    file_stream.cpp
    file_stream_service.cpp
    file_write_channel.cpp
)
