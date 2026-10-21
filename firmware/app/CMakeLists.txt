# Coglet firmware: ESP-IDF app that speaks agent-hub's xiaozhi wire protocol.
# The servo controller is pulled in as a component from ../coglet_controller.
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.1")
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../coglet_controller)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(coglet)
