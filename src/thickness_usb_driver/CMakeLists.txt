cmake_minimum_required(VERSION 3.8)
project(thickness_usb_driver)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rcl_interfaces REQUIRED)
find_package(robot_interfaces REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(thickness_usb_port STATIC
  module/UsbCdcPort.cpp
  module/ThicknessProtocol.cpp
)
target_compile_features(thickness_usb_port PUBLIC cxx_std_17)
target_include_directories(thickness_usb_port PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

add_executable(thickness_node src/ThicknessNode.cpp)
target_link_libraries(thickness_node thickness_usb_port)
ament_target_dependencies(thickness_node rclcpp rcl_interfaces robot_interfaces)
install(TARGETS thickness_usb_port ARCHIVE DESTINATION lib)
install(TARGETS thickness_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_thickness_protocol test/test_thickness_protocol.cpp)
  target_link_libraries(test_thickness_protocol thickness_usb_port)
  add_test(NAME thickness_protocol COMMAND test_thickness_protocol)

  add_executable(test_usb_port test/test_usb_port.cpp)
  target_link_libraries(test_usb_port thickness_usb_port util)
  add_test(NAME usb_port COMMAND test_usb_port)
endif()

ament_package()
