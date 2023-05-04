cmake_minimum_required(VERSION 2.8)

project(webserver)

aux_source_directory(. SRC_LIST)

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR})

add_executable(server ${SRC_LIST})
TARGET_LINK_LIBRARIES(server pthread)