cmake_minimum_required(VERSION 3.8)
project(measurement_manager)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(robot_interfaces REQUIRED)
add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(measurement_manager_node src/MeasurementManagerNode.cpp)
target_compile_features(measurement_manager_node PRIVATE cxx_std_17)
target_include_directories(measurement_manager_node PRIVATE include)
ament_target_dependencies(measurement_manager_node rclcpp robot_interfaces)

install(TARGETS measurement_manager_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY include/ DESTINATION include)
install(DIRECTORY launch DESTINATION share/${PROJECT_NAME})

if(BUILD_TESTING)
  enable_testing()
  add_executable(test_measurement_manager test/test_measurement_manager.cpp src/MeasurementManagerNode.cpp)
  target_compile_features(test_measurement_manager PRIVATE cxx_std_17)
  target_compile_definitions(test_measurement_manager PRIVATE MEASUREMENT_MANAGER_NO_MAIN)
  target_include_directories(test_measurement_manager PRIVATE include)
  ament_target_dependencies(test_measurement_manager rclcpp robot_interfaces)
  add_test(NAME measurement_manager COMMAND test_measurement_manager)
  set_tests_properties(measurement_manager PROPERTIES ENVIRONMENT "ROS_DOMAIN_ID=78" TIMEOUT 30)
endif()

ament_package()
