# CMake generated Testfile for 
# Source directory: C:/Users/jlynb/CLionProjects/HackUTD/tests/native
# Build directory: C:/Users/jlynb/CLionProjects/HackUTD/tests/native/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if(CTEST_CONFIGURATION_TYPE MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(faultDetect_tests "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/build/Debug/fault_tests.exe")
  set_tests_properties(faultDetect_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;25;add_test;C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(faultDetect_tests "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/build/Release/fault_tests.exe")
  set_tests_properties(faultDetect_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;25;add_test;C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(faultDetect_tests "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/build/MinSizeRel/fault_tests.exe")
  set_tests_properties(faultDetect_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;25;add_test;C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;0;")
elseif(CTEST_CONFIGURATION_TYPE MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(faultDetect_tests "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/build/RelWithDebInfo/fault_tests.exe")
  set_tests_properties(faultDetect_tests PROPERTIES  _BACKTRACE_TRIPLES "C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;25;add_test;C:/Users/jlynb/CLionProjects/HackUTD/tests/native/CMakeLists.txt;0;")
else()
  add_test(faultDetect_tests NOT_AVAILABLE)
endif()
