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

ament_package()
