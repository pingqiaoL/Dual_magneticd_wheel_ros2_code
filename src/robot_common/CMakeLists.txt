cmake_minimum_required(VERSION 3.8)
project(robot_common)

find_package(ament_cmake REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(robot_common STATIC
  module/SteadyClock.cpp
  module/SerialPort.cpp
)
target_compile_features(robot_common PUBLIC cxx_std_17)
target_include_directories(robot_common PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

install(
  TARGETS robot_common
  EXPORT export_robot_common
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
  INCLUDES DESTINATION include
)
install(DIRECTORY include/ DESTINATION include)

ament_export_targets(export_robot_common HAS_LIBRARY_TARGET)
ament_export_include_directories(include)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_robot_common test/test_robot_common.cpp)
  target_link_libraries(test_robot_common robot_common)
  add_test(NAME robot_common COMMAND test_robot_common)

  add_executable(test_serial_port test/test_serial_port.cpp)
  target_link_libraries(test_serial_port robot_common util)
  add_test(NAME serial_port COMMAND test_serial_port)
endif()

ament_package()
