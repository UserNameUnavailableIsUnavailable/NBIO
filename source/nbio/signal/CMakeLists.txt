cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    system_signal.cpp
    system_signal_channel.cpp
)
