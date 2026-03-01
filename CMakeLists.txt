cmake_minimum_required(VERSION 3.10)
project(adsb-module CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(libs/adsb-cpp-lib)

add_executable(adsb-module
        src/main.cpp
        src/RTLSDRDevice.cpp
        src/ADSBProcessor.cpp
        src/MQTTPublisher.cpp
        include/DataQueue.hpp
)

target_include_directories(adsb-module PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_library(PAHO_MQTT_CPP_LIBRARY
        NAMES paho-mqttpp3
        HINTS /usr/lib
        REQUIRED
)

find_library(PAHO_MQTT_C_LIBRARY
        NAMES paho-mqtt3a
        HINTS /usr/lib
        REQUIRED
)

target_link_libraries(adsb-module PRIVATE
        yaml-cpp
        adsb-lib
        rtlsdr
        pthread
        m
        ssl
        crypto
        ${PAHO_MQTT_CPP_LIBRARY}
        ${PAHO_MQTT_C_LIBRARY}
)
