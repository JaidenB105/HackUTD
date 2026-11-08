cmake_minimum_required(VERSION 3.16)
project(ResilientSenseHostTests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

enable_testing()

# Fault-detector suite.
add_executable(fault_detector_tests
        ../../main/faultDetect.c
        testFaultDetect.c
)
target_include_directories(fault_detector_tests PRIVATE ../../main mocks)

# Sensor-history suite.
add_executable(sensor_history_tests
        ../../main/sensor_history.c
        testSensorHist.c
)
target_include_directories(sensor_history_tests PRIVATE ../../main mocks)

add_executable(diagnostic_engine_tests
        ../../main/sensor_history.c
        ../../main/diagnostic_engine.c
        test_diagnostic_engine.c
)
target_include_directories(diagnostic_engine_tests PRIVATE ../../main mocks)

foreach(target fault_detector_tests sensor_history_tests diagnostic_engine_tests)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(${target} PRIVATE m)
    endif()
endforeach()

add_test(NAME fault_detector_tests COMMAND fault_detector_tests)
add_test(NAME sensor_history_tests COMMAND sensor_history_tests)
add_test(NAME diagnostic_engine_tests COMMAND diagnostic_engine_tests)
