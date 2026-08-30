# Sexlab Scene Camera

Native SKSE camera-controller proof of concept for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin asks SmoothCam 1.7.1 for camera ownership. While active, a deliberately impractical Core rig places the camera 3000 game units above the participants' center and points it straight down. On the matching `AnimationEnd`, it asks SmoothCam to resume at its current goal and releases ownership.

The POC has no ESP and no dedicated Papyrus script. SexLab P+ integration uses the native SKSE `ModCallbackEvent` dispatcher. P+ currently emits unprefixed `AnimationStart`/`AnimationEnd` compatibility events alongside its documented Papyrus `HookAnimationStart`/`HookAnimationEnd` API. Because P+ passes `thread_id` as the second `SendModEvent` argument, the native adapter parses it from `strArg` and treats `numArg` only as a compatibility fallback.

## Requirements

- Skyrim SE/AE (VR is intentionally rejected by this POC)
- SKSE64 and Address Library for the active runtime
- SexLab P+
- SmoothCam 1.7.1

For an unambiguous test, disable SexLab's automatic free-camera/TFC option so it does not compete with the POC after the scene starts.

## Build

Initialize only the required submodules, then configure from a Visual Studio 2022 developer shell. Do not use a recursive submodule update: SmoothCam has many build-only nested dependencies, while this project needs only its API header. CommonLibSSE-NG's OpenVR submodule is required by its default multi-runtime source build.

```powershell
git submodule update --init external/smoothcam lib/commonlibsse-ng
git -C lib/commonlibsse-ng submodule update --init extern/openvr
$env:COMMONLIB_PREBUILT = '1'
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --config RelWithDebInfo --target SexlabSceneCamera SexlabSceneCameraCoreTests
ctest --test-dir build -C RelWithDebInfo --output-on-failure
```

Install `SexlabSceneCamera.dll` under `Data/SKSE/Plugins`. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## POC validation

1. Start the game with SexLab P+, SmoothCam, and this DLL enabled.
2. Start a SexLab scene containing the player.
3. Confirm the camera jumps far above the participants and points downward.
4. End the scene and confirm normal SmoothCam control returns.
5. Start an NPC-only scene and confirm the camera is not taken.
6. Inspect `SexlabSceneCamera.log` for event order, scene keys, ownership results, and restoration.

The compiled DLL proves only that the native interfaces and code agree at build time. The event payload, camera-node behavior, and restoration sequence still require the in-game validation above.
