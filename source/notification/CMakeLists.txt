cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    notifier.cpp
    condition_variable.cpp
    notifier_channel.cpp
)
