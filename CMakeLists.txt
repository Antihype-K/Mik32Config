cmake_minimum_required(VERSION 4.0)

project(MIK32-configurator)

add_executable(Configurator-Executable)

target_sources(Configurator-Executable
    PRIVATE
        src/main.cpp
)

add_library(Configurator-Library INTERFACE)
target_include_directories(Configurator-Library 
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Configurator-Executable PRIVATE Configurator-Library)
