cmake_minimum_required(VERSION 3.20)

target_sources(
    NBIO
    PRIVATE
    EventNotifier.cpp
    ConditionVariable.cpp
    EventNotifyChannel.cpp
)
