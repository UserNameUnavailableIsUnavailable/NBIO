cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    Buffer.cpp
    BitmapMemory.cpp
    ThreadMemoryPool.cpp
)