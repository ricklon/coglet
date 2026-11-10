# Coglet firmware: ESP-IDF app that speaks agent-hub's xiaozhi wire protocol.
# The servo controller is pulled in as a component from ../coglet_controller.
cmake_minimum_required(VERSION 3.16)

set(PROJECT_VER "0.1.3")
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../coglet_controller)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# The C6 build has its own component lock, so building it never rewrites the C3's.
if(IDF_TARGET STREQUAL "esp32c6")
    idf_build_set_property(DEPENDENCIES_LOCK "${CMAKE_CURRENT_LIST_DIR}/dependencies.lock.esp32c6")
endif()
project(coglet)
