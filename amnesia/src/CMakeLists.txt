cmake_minimum_required (VERSION 2.8.11)
project(Lux)

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR})

OPTION(USERDIR_RESOURCES "Allow using local userdirectory resources" ON)

set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2010-2013 Frictional Games")

OPTION(IS_APPLE_64 "Apple build in 64 bits" ON)

if(APPLE)
    if(IS_APPLE_64)
        set(CMAKE_OSX_ARCHITECTURES "x86_64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "i386")
    endif()
endif()

# silence some annoying warnings
add_definitions(
    -Wno-switch
    -Wno-reorder
    -Wno-unused-variable
    -Wno-unused-function
    -Wno-c++11-narrowing
)

add_subdirectory(../../HPL2/core core2)

if(USERDIR_RESOURCES)
    add_definitions(
        -DUSERDIR_RESOURCES
    )
endif()

set(VERSION "1.3.1")

add_subdirectory(game game)

if(APPLE)
    # Off by default: Launcher_Prefix.pch is missing from the source release
    OPTION(BUILD_MACOSX_LAUNCHER "Build the macOS launcher" OFF)
    if(BUILD_MACOSX_LAUNCHER)
        add_subdirectory(launcher-macosx launcher)
    endif()
else()
    add_subdirectory(launcher launcher)
endif()

add_custom_target(GameRelease
    DEPENDS Amnesia Launcher
)
