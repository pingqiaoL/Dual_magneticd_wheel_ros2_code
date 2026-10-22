cmake_minimum_required(VERSION 3.8)
project(telemetry_bridge)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(robot_common REQUIRED)
find_package(robot_interfaces REQUIRED)
find_package(Threads REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(telemetry_driver STATIC module/TelemetryDriver.cpp module/RemoteProtocol.cpp)
target_compile_features(telemetry_driver PUBLIC cxx_std_17)
target_include_directories(telemetry_driver PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)
ament_target_dependencies(telemetry_driver robot_common)
target_link_libraries(telemetry_driver Threads::Threads)

add_executable(telemetry_bridge_node src/TelemetryBridgeNode.cpp)
target_link_libraries(telemetry_bridge_node telemetry_driver)
ament_target_dependencies(telemetry_bridge_node rclcpp rcl_interfaces robot_interfaces)

install(TARGETS telemetry_driver ARCHIVE DESTINATION lib)
install(TARGETS telemetry_bridge_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_remote_protocol test/test_remote_protocol.cpp)
  target_link_libraries(test_remote_protocol telemetry_driver)
  add_test(NAME remote_protocol COMMAND test_remote_protocol)
  add_executable(test_telemetry_driver test/test_telemetry_driver.cpp)
  target_link_libraries(test_telemetry_driver telemetry_driver util)
  add_test(NAME telemetry_driver COMMAND test_telemetry_driver)
endif()

ament_package()
