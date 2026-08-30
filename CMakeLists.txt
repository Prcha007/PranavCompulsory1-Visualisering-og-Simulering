cmake_minimum_required(VERSION 3.19)
project(3Dprog26 LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect build environment based on CMAKE_BINARY_DIR
# Used to set correct PATH and some clean up
if(${CMAKE_BINARY_DIR} MATCHES ".*out/.*")
    add_compile_definitions(BUILD_ENV_VISUAL_STUDIO)
    message(STATUS "=== Visual Studio detected ===")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build")
        file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/build")
    endif()
elseif(${CMAKE_BINARY_DIR} MATCHES ".*build/.*")
    add_compile_definitions(BUILD_ENV_QTCREATOR)
    message(STATUS "=== Qt Creator detected ===")
    #Deleting preset files not used in Qt - generated in VS 2022
    #This should not fail even if the files are not present
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakePresets.json")
    file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/CMakeUserPresets.json")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out")
        file(REMOVE_RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/out")
    endif()
endif()

# Source files (include the .ui so AUTOUIC processes it)
set(SOURCES
    main.cpp
    Renderer.h Renderer.cpp
    Utilities.h
    Logger.h Logger.cpp
    MainWindow.h MainWindow.cpp
    MainWindow.ui
    MacMetalBridge.mm
)

# all shader and script files shown in IDE
file(GLOB SHADER_FILES
    "Shaders/*.frag"
    "Shaders/*.vert"
    "Shaders/*.bat"
    "Shaders/*.sh"
)

add_executable(3Dprog26 ${SOURCES} ${SHADER_FILES})

if(MSVC)
    # Required for proper C++ exception handling with MSVC
    target_compile_options(3Dprog26 PRIVATE /EHsc)
    target_include_directories(3Dprog26 PRIVATE
        $ENV{VULKAN_SDK}/Include
    )
    target_link_libraries(3Dprog26 PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Widgets
        $ENV{VULKAN_SDK}/Lib/vulkan-1.lib
    )
elseif(APPLE)
    # This will set NDEBUG for both debug and release build
    # It stops Validation layers from being used - needs to be fixed!!!
    add_compile_definitions(NDEBUG)

    target_include_directories(3Dprog26 PRIVATE
        #Hardcoded path for Oles Macs for now:
        "/Users/ole/VulkanSDK/1.4.335.1/macOS/include"
    )
    target_link_libraries(3Dprog26 PRIVATE
        Qt::Core
        Qt::Widgets
        "-framework QuartzCore"
        "-framework Metal"
        "-framework Foundation"
        "-framework IOSurface"
        "-framework CoreGraphics"
        "-framework IOKit"
        "-framework AppKit"
        #Hardcoded path for Oles Macs for now:
        "/Users/ole/VulkanSDK/1.4.335.1/macOS/lib/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a"
    )
endif()

if(APPLE)
    add_custom_target(CompileShaders
        COMMENT "Compiling Vulkan shaders for macOS"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
        COMMAND bash CompileShadersMac.sh
    )
elseif(MSVC)
    add_custom_target(CompileShaders
        COMMENT "Compiling Vulkan shaders for Windows"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
        COMMAND "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe" -V "Shader.vert" -o "vert.spv"
        COMMAND "$ENV{VULKAN_SDK}/Bin/glslangValidator.exe" -V "Shader.frag" -o "frag.spv"
    )
endif()

add_dependencies(3Dprog26 CompileShaders)
