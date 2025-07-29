cmake_minimum_required(VERSION 3.12)

project(NDI)

add_subdirectory(NDISimpleTestSender)
add_dependencies(NDISimpleTestSender GLBase SceneGraph AudioVideo)

add_subdirectory(NDISimpleReceiver)
add_dependencies(NDISimpleReceiver GLBase SceneGraph AudioVideo)

add_subdirectory(NDISenderCameraInput)
add_dependencies(NDISenderCameraInput GLBase SceneGraph AudioVideo)