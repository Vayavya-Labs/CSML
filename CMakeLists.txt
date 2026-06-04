cmake_minimum_required(VERSION 3.14)
project(csml LANGUAGES CXX)

# SystemC::systemc target is provided by the parent build system.
# CSML does not manage SystemC discovery — that is the consuming project's responsibility.
add_library(csml_logger STATIC src/csml_logger.cpp)

target_include_directories(csml_logger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_link_libraries(csml_logger PUBLIC SystemC::systemc)

message(STATUS "CSML logger library configured")
