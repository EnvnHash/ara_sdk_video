cmake_minimum_required(VERSION 3.12)

add_subdirectory(Realsense_Basic)
add_dependencies(Realsense_Basic Utilities GLBase SceneGraph AudioVideo)
