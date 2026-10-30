cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF CACHE BOOL "Treat compiler warnings as errors" FORCE)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE
    src/plugin-main.cpp
    src/control-dock.cpp
    src/control-dock.hpp
)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    PREFIX ""
    OUTPUT_NAME "${_name}"
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME "${_name}")
