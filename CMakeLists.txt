cmake_minimum_required(VERSION 3.16)
project(MidiMaster2)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt (required by Drumstick)
find_package(Qt6 QUIET COMPONENTS Core Widgets Test)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets Test)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

# Include FetchContent for downloading dependencies
include(FetchContent)

# Fetch RTMidi from GitHub (for MIDI input - better real-time performance)
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG 6.0.0
    GIT_SHALLOW TRUE
)

# Fetch Drumstick from GitHub (for MIDI output - keeping for now)
FetchContent_Declare(
    drumstick
    GIT_REPOSITORY https://github.com/pedrolcl/drumstick.git
    GIT_TAG devel
    GIT_SHALLOW TRUE
)

# Fetch RTMidi
FetchContent_GetProperties(rtmidi)
if(NOT rtmidi_POPULATED)
    FetchContent_Populate(rtmidi)
    # RTMidi is header-only, we just need the include path
endif()

# Get the source but don't add it yet  
FetchContent_GetProperties(drumstick)
if(NOT drumstick_POPULATED)
    FetchContent_Populate(drumstick)

    # Force static linking for Drumstick backends
    set(STATIC_DRUMSTICK ON CACHE BOOL "Build Drumstick as static libraries" FORCE)
    set(BUILD_FRAMEWORKS OFF CACHE BOOL "Build macOS frameworks" FORCE)

    # Create cmake_admin directory and file in the drumstick source
    file(MAKE_DIRECTORY "${drumstick_SOURCE_DIR}/cmake_admin")
    if(NOT EXISTS "${drumstick_SOURCE_DIR}/cmake_admin/cmake_uninstall.cmake.in")
        file(WRITE "${drumstick_SOURCE_DIR}/cmake_admin/cmake_uninstall.cmake.in"
            "# CMake uninstall target\n")
    endif()

    # Create it in project root where Drumstick CMakeLists incorrectly looks for it
    # This is a workaround for a path bug in Drumstick's CMakeLists.txt when used as subdirectory
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/cmake_admin")
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cmake_admin/cmake_uninstall.cmake.in")
        file(WRITE "${CMAKE_SOURCE_DIR}/cmake_admin/cmake_uninstall.cmake.in"
            "# CMake uninstall target\n")
    endif()

    # Add drumstick as a subdirectory
    add_subdirectory(${drumstick_SOURCE_DIR} ${drumstick_BINARY_DIR} EXCLUDE_FROM_ALL)

    # Restore BUILD_SHARED_LIBS
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "Build shared libraries" FORCE)
endif()

# Add RTMidi source files
# RTMidi needs to be compiled - add the source file
set(RTMIDI_SOURCES "")

# Fetch RTMidi source if not already populated
FetchContent_GetProperties(rtmidi)
if(NOT rtmidi_POPULATED)
    FetchContent_Populate(rtmidi)
endif()

# Add RTMidi source file (check common locations)
if(EXISTS "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
    list(APPEND RTMIDI_SOURCES "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
elseif(EXISTS "${rtmidi_SOURCE_DIR}/rtmidi/RtMidi.cpp")
    list(APPEND RTMIDI_SOURCES "${rtmidi_SOURCE_DIR}/rtmidi/RtMidi.cpp")
else()
    message(WARNING "RtMidi.cpp not found. Checking ${rtmidi_SOURCE_DIR}")
    # Try to find it
    file(GLOB_RECURSE RTMIDI_CPP "${rtmidi_SOURCE_DIR}/*/RtMidi.cpp" "${rtmidi_SOURCE_DIR}/RtMidi.cpp")
    if(RTMIDI_CPP)
        list(GET RTMIDI_CPP 0 RTMIDI_CPP_FILE)
        list(APPEND RTMIDI_SOURCES "${RTMIDI_CPP_FILE}")
        message(STATUS "Found RtMidi.cpp at ${RTMIDI_CPP_FILE}")
    else()
        message(FATAL_ERROR "Could not find RtMidi.cpp in ${rtmidi_SOURCE_DIR}")
    endif()
endif()

# Add executable with source files from multiple folders
add_executable(MidiMaster2
    main.cpp
    lib/ui/MidiMasterWindow.cpp
    lib/midiEngine/MidiEngine.cpp
    lib/midiEngine/SyncController.cpp
    ${RTMIDI_SOURCES}
)

# Define DRUMSTICK_STATIC for static plugin linking
target_compile_definitions(MidiMaster2 PRIVATE DRUMSTICK_STATIC)

# Define RTMidi API support for macOS (required for CoreMIDI)
if(APPLE)
    target_compile_definitions(MidiMaster2 PRIVATE __MACOSX_CORE__)
endif()

# Add include directories for project headers
target_include_directories(MidiMaster2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/ui
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/midiEngine
    ${rtmidi_SOURCE_DIR}
)

# Link CoreMIDI, CoreAudio, and CoreFoundation frameworks on macOS (required by RTMidi)
if(APPLE)
    find_library(COREMIDI_FRAMEWORK CoreMIDI)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
    if(COREMIDI_FRAMEWORK)
        target_link_libraries(MidiMaster2 PRIVATE ${COREMIDI_FRAMEWORK})
    endif()
    if(COREAUDIO_FRAMEWORK)
        target_link_libraries(MidiMaster2 PRIVATE ${COREAUDIO_FRAMEWORK})
    endif()
    if(COREFOUNDATION_FRAMEWORK)
        target_link_libraries(MidiMaster2 PRIVATE ${COREFOUNDATION_FRAMEWORK})
    endif()
endif()

# Link required libraries
# Try different possible target names for drumstick-rt
if(TARGET drumstick::drumstick-rt)
    target_link_libraries(MidiMaster2 PRIVATE drumstick::drumstick-rt)
elseif(TARGET drumstick-rt)
    target_link_libraries(MidiMaster2 PRIVATE drumstick-rt)
else()
    message(FATAL_ERROR "Could not find drumstick-rt target. Available targets may differ.")
endif()

# Link static backend plugins for macOS (required when DRUMSTICK_STATIC is defined)
# Only link if targets exist and are STATIC libraries
function(link_backend_if_static target_name)
    if(TARGET ${target_name})
        get_target_property(target_type ${target_name} TYPE)
        if(target_type STREQUAL "STATIC_LIBRARY")
            target_link_libraries(MidiMaster2 PRIVATE ${target_name})
        endif()
    endif()
endfunction()

link_backend_if_static(drumstick-rt-mac-out)
link_backend_if_static(drumstick-rt-mac-in)
link_backend_if_static(drumstick-rt-net-out)
link_backend_if_static(drumstick-rt-net-in)
link_backend_if_static(drumstick-rt-fluidsynth)

if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(MidiMaster2 PRIVATE Qt6::Core Qt6::Widgets)
else()
    target_link_libraries(MidiMaster2 PRIVATE Qt5::Core Qt5::Widgets)
endif()

# Enable automatic MOC, UIC, and RCC processing for Qt
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Ensure MOC runs for the executable
set_target_properties(MidiMaster2 PROPERTIES
    AUTOMOC ON
)

# Add test executable
enable_testing()

add_executable(SyncControllerTest
    lib/midiEngine/SyncControllerTest.cpp
    lib/midiEngine/SyncController.cpp
    lib/midiEngine/MidiEngine.cpp
    ${RTMIDI_SOURCES}
)

target_include_directories(SyncControllerTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/midiEngine
    ${rtmidi_SOURCE_DIR}
)

# Link Qt Test framework and required libraries
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(SyncControllerTest PRIVATE Qt6::Core Qt6::Test Qt6::Widgets)
else()
    target_link_libraries(SyncControllerTest PRIVATE Qt5::Core Qt5::Test Qt5::Widgets)
endif()

# Link Drumstick for MidiEngine
if(TARGET drumstick::drumstick-rt)
    target_link_libraries(SyncControllerTest PRIVATE drumstick::drumstick-rt)
elseif(TARGET drumstick-rt)
    target_link_libraries(SyncControllerTest PRIVATE drumstick-rt)
endif()

# Link static backend plugins for macOS
link_backend_if_static(drumstick-rt-mac-out)
link_backend_if_static(drumstick-rt-mac-in)

# Link CoreMIDI, CoreAudio, and CoreFoundation frameworks on macOS
if(APPLE)
    if(COREMIDI_FRAMEWORK)
        target_link_libraries(SyncControllerTest PRIVATE ${COREMIDI_FRAMEWORK})
    endif()
    if(COREAUDIO_FRAMEWORK)
        target_link_libraries(SyncControllerTest PRIVATE ${COREAUDIO_FRAMEWORK})
    endif()
    if(COREFOUNDATION_FRAMEWORK)
        target_link_libraries(SyncControllerTest PRIVATE ${COREFOUNDATION_FRAMEWORK})
    endif()
endif()

# Define DRUMSTICK_STATIC for static plugin linking
target_compile_definitions(SyncControllerTest PRIVATE DRUMSTICK_STATIC)

# Define RTMidi API support for macOS
if(APPLE)
    target_compile_definitions(SyncControllerTest PRIVATE __MACOSX_CORE__)
endif()

# Enable MOC for test
set_target_properties(SyncControllerTest PROPERTIES
    AUTOMOC ON
)

# Add test to CTest
add_test(NAME SyncControllerTest COMMAND SyncControllerTest)
