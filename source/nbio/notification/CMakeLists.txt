cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    event_notifier.cpp
    condition_variable.cpp
    event_notify_channel.cpp
)
