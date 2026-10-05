cmake_minimum_required(VERSION 3.20)

target_sources(
    nbio
    PRIVATE
    EventNotifier.cpp
    ConditionVariable.cpp
    EventNotifyChannel.cpp
)
