cmake_minimum_required(VERSION 3.24)

# SKSE loads plugins from the game process, where unrelated mods may ship a
# different fmt/spdlog DLL. Link vcpkg libraries statically while retaining the
# release dynamic CRT so the plugin has no private runtime DLL dependencies.
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "vcpkg target triplet")
endif()

project(
    SexlabSceneCamera
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

option(SSC_BUILD_TESTS "Build unit tests" ON)
option(SSC_ENABLE_DEBUG_ANCHOR "Show a direction arrow at the scene anchor" ON)

# The POC uses a vtable hook and does not allocate a trampoline. Keep CommonLib's
# own tests and patch-safety-only dependency out of this consumer build.
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_PATCH_SAFETY OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/commonlibsse-ng)

# CommonLibSSE-NG's prebuilt path includes this helper itself. Its source-build
# fallback currently does not, so consumers must make the helper available.
if(NOT COMMAND add_commonlibsse_plugin)
    include(lib/commonlibsse-ng/cmake/CommonLibSSE.cmake)
endif()

set(SSC_SOURCES
    src/SceneCamera.cpp
    src/SceneCamera.h
    src/SceneSession.h
    src/core/CameraTypes.h
    src/core/SceneAnchor.cpp
    src/core/SceneAnchor.h
    src/runtime/CameraHook.cpp
    src/runtime/CameraHook.h
    src/runtime/CameraOutput.cpp
    src/runtime/CameraOutput.h
    src/runtime/ICameraControl.h
    src/runtime/IDebugVisualization.h
    src/runtime/IRuntimeClient.h
    src/runtime/ISceneSource.h
    src/runtime/PluginRuntime.cpp
    src/runtime/PluginRuntime.h
    src/runtime/SceneEvent.h
    src/runtime/SceneParticipantSnapshotStorage.h
    src/runtime/SceneEventMailbox.cpp
    src/runtime/SceneEventMailbox.h
    src/runtime/SceneKey.h
    src/runtime/RuntimeTypes.h
    src/runtime/SexLabPSceneSource.cpp
    src/runtime/SexLabPSceneSource.h
    src/runtime/SmoothCamCameraControl.cpp
    src/runtime/SmoothCamCameraControl.h
    src/runtime/WorldDebugVisualization.cpp
    src/runtime/WorldDebugVisualization.h
    src/main.cpp
    src/pch.h
)

add_commonlibsse_plugin(
    SexlabSceneCamera
    NAME "Sexlab Scene Camera"
    AUTHOR "Sexlab Scene Camera contributors"
    VERSION ${PROJECT_VERSION}
    SOURCES ${SSC_SOURCES}
)

target_include_directories(
    SexlabSceneCamera
    PRIVATE
        src
        external/smoothcam/SmoothCam/include
)

target_compile_definitions(
    SexlabSceneCamera
    PRIVATE
        SMOOTHCAM_API_COMMONLIB
        WIN32_LEAN_AND_MEAN
        NOMINMAX
)

if(SSC_ENABLE_DEBUG_ANCHOR)
    target_compile_definitions(SexlabSceneCamera PRIVATE SSC_ENABLE_DEBUG_ANCHOR=1)
endif()

target_precompile_headers(SexlabSceneCamera PRIVATE src/pch.h)

if(MSVC)
    target_compile_options(
        SexlabSceneCamera
        PRIVATE
            /EHsc
            /MP
            /permissive-
            /W4
            /Zc:preprocessor
    )
endif()

set_target_properties(
    SexlabSceneCamera
    PROPERTIES
        OUTPUT_NAME "SexlabSceneCamera"
)

if(SSC_BUILD_TESTS)
    enable_testing()
    add_executable(
        SexlabSceneCameraTests
        tests/SceneSessionTests.cpp
        src/core/SceneAnchor.cpp
        src/core/SceneAnchor.h
        src/SceneSession.h
    )
    target_include_directories(SexlabSceneCameraTests PRIVATE src)
    if(MSVC)
        target_compile_options(
            SexlabSceneCameraTests
            PRIVATE
                /EHsc
                /permissive-
                /W4
        )
    endif()
    add_test(
        NAME SexlabSceneCamera.SceneSession
        COMMAND SexlabSceneCameraTests
    )
endif()
