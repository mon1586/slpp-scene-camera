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

option(SSC_BUILD_TESTS "Build Controller layer unit tests" ON)

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
    src/controller/CameraHook.cpp
    src/controller/CameraHook.h
    src/controller/CameraOutput.cpp
    src/controller/CameraOutput.h
    src/controller/ICameraController.h
    src/controller/ISceneController.h
    src/controller/SceneCameraCoordinator.cpp
    src/controller/SceneCameraCoordinator.h
    src/controller/SceneEvent.h
    src/controller/SceneEventMailbox.cpp
    src/controller/SceneEventMailbox.h
    src/controller/SceneKey.h
    src/controller/SceneSession.h
    src/controller/SexLabPSceneController.cpp
    src/controller/SexLabPSceneController.h
    src/controller/SmoothCamCameraController.cpp
    src/controller/SmoothCamCameraController.h
    src/core/CameraTypes.h
    src/core/HighAltitudeRig.cpp
    src/core/HighAltitudeRig.h
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
        SexlabSceneCameraControllerTests
        tests/SceneSessionTests.cpp
        src/controller/SceneSession.h
    )
    target_include_directories(SexlabSceneCameraControllerTests PRIVATE src)
    if(MSVC)
        target_compile_options(
            SexlabSceneCameraControllerTests
            PRIVATE
                /EHsc
                /permissive-
                /W4
        )
    endif()
    add_test(
        NAME SexlabSceneCamera.Controller.SceneSession
        COMMAND SexlabSceneCameraControllerTests
    )
endif()
