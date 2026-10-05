cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    File.cpp
    FileView.cpp
    FileReadChannel.cpp
    FileStream.cpp
    FileStreamService.cpp
    FileWriteChannel.cpp
)
