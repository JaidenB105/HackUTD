
cmake_minimum_required(VERSION 3.16)

project(ResilientSenseTests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

add_executable(fault_tests
        testFaultDetect.c
        ../../main/faultDetect.c
)

target_include_directories(fault_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/mocks"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../main"
)

if(NOT MSVC)
    target_link_libraries(fault_tests PRIVATE m)
endif()

add_test(
        NAME faultDetect_tests
        COMMAND fault_tests
)
