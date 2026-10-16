cmake_minimum_required(VERSION 3.8)
project(thickness_usb_driver)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(robot_common REQUIRED)
find_package(robot_interfaces REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(thickness_device STATIC
  module/ThicknessDevice.cpp
  module/ThicknessProtocol.cpp
)
target_compile_features(thickness_device PUBLIC cxx_std_17)
target_include_directories(thickness_device PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(thickness_device robot_common)

add_executable(thickness_node src/ThicknessNode.cpp)
target_link_libraries(thickness_node thickness_device)
ament_target_dependencies(thickness_node rclcpp rcl_interfaces robot_interfaces)

install(TARGETS thickness_device ARCHIVE DESTINATION lib)
install(TARGETS thickness_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
ament_export_dependencies(robot_common)

if(BUILD_TESTING)
  enable_testing()

  add_executable(test_thickness_protocol test/test_thickness_protocol.cpp)
  target_link_libraries(test_thickness_protocol thickness_device)
  add_test(NAME thickness_protocol COMMAND test_thickness_protocol)

  add_executable(test_thickness_device test/test_thickness_device.cpp)
  target_link_libraries(test_thickness_device thickness_device util)
  add_test(NAME thickness_device COMMAND test_thickness_device)
endif()

ament_package()
