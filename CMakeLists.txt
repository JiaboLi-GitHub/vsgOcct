cmake_minimum_required(VERSION 3.20)

project(vsgocct LANGUAGES CXX)

option(VSGOCCT_BUILD_EXAMPLES "Build vsgocct example applications." ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_MAP_IMPORTED_CONFIG_RELEASE Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_RELWITHDEBINFO Debug)
set(CMAKE_MAP_IMPORTED_CONFIG_MINSIZEREL Debug)

if(CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_CONFIGURATION_TYPES Debug CACHE STRING "Supported build types." FORCE)
endif()

if(NOT DEFINED QT_PACKAGE_NAME)
    file(GLOB QT6_CONFIG_FILES LIST_DIRECTORIES FALSE "C:/Qt/*/*/lib/cmake/Qt6/Qt6Config.cmake")
    list(SORT QT6_CONFIG_FILES COMPARE NATURAL ORDER DESCENDING)

    if(QT6_CONFIG_FILES)
        list(GET QT6_CONFIG_FILES 0 QT6_CONFIG_FILE)
        get_filename_component(QT6_CMAKE_DIR "${QT6_CONFIG_FILE}" DIRECTORY)
        get_filename_component(QT6_PREFIX "${QT6_CMAKE_DIR}/../../.." ABSOLUTE)
        list(PREPEND CMAKE_PREFIX_PATH "${QT6_PREFIX}")
        set(QT_PACKAGE_NAME Qt6 CACHE STRING "Qt package name, e.g. Qt5 or Qt6." FORCE)
    else()
        set(QT_PACKAGE_NAME Qt5 CACHE STRING "Qt package name, e.g. Qt5 or Qt6.")
    endif()
endif()
set(VSGQT_BUILD_EXAMPLES OFF CACHE BOOL "Disable bundled vsgQt examples." FORCE)

if(WIN32)
    add_definitions(-DNOMINMAX)
endif()

find_package(vsg 1.1.2 REQUIRED)

if(EXISTS "D:/OCCT/build/OpenCASCADETargets.cmake")
    include("D:/OCCT/build/OpenCASCADETargets.cmake")
    set(OpenCASCADE_INCLUDE_DIR "D:/OCCT/build/inc")
else()
    find_package(OpenCASCADE CONFIG REQUIRED COMPONENTS FoundationClasses ModelingData ModelingAlgorithms DataExchange)
endif()

function(vsgocct_deploy_qt target_name)
    if(NOT WIN32)
        return()
    endif()

    if(QT_PACKAGE_NAME STREQUAL "Qt6")
        get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    elseif(QT_PACKAGE_NAME STREQUAL "Qt5")
        get_target_property(QT_QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
    endif()

    if(QT_QMAKE_EXECUTABLE)
        get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_BIN_DIR}")
    endif()

    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --debug --dir "$<TARGET_FILE_DIR:${target_name}>" "$<TARGET_FILE:${target_name}>"
            COMMENT "Deploying Qt runtime and platform plugins for ${target_name}"
        )
    endif()
endfunction()

add_subdirectory(src)

if(VSGOCCT_BUILD_EXAMPLES)
    add_subdirectory(D:/vsgQt ${CMAKE_BINARY_DIR}/vsgQt)
    add_subdirectory(examples)
endif()
