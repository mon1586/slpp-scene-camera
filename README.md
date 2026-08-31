# Sexlab Scene Camera

Native SKSE camera-controller proof of concept for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin captures a fixed scene anchor from the participants' Pelvis nodes after the camera update boundary. Its position is the Pelvis average; with multiple participants its forward points from that average toward the player Pelvis, and otherwise it falls back to the inverse of the player's horizontal forward. Camera presets and collision/raycast selection are not implemented yet, so the plugin deliberately leaves SmoothCam in control rather than placing a camera inside the scene anchor. A matching `AnimationEnding` or `AnimationEnd` discards the anchor; lifecycle resets, invalid participants, and a watchdog provide additional fail-safe exits.

The POC has no ESP and no dedicated Papyrus script. SexLab P+ integration uses the native SKSE `ModCallbackEvent` dispatcher. P+ currently emits unprefixed `AnimationStart`/`AnimationEnd` compatibility events alongside its documented Papyrus `HookAnimationStart`/`HookAnimationEnd` API. Because P+ passes `thread_id` as the second `SendModEvent` argument, the native adapter parses it from `strArg` and treats `numArg` only as a compatibility fallback. Generic event names are accepted only when the sender is a quest defined by `SexLab.esm`.

## Requirements

- Skyrim SE/AE (VR is intentionally rejected by this POC)
- SKSE64 and Address Library for the active runtime
- SexLab P+
- SmoothCam 1.7.1

For an unambiguous test, disable SexLab's automatic free-camera/TFC option so it does not compete with the POC after the scene starts.

## Build

Initialize only the required submodules. Do not use a recursive submodule update: SmoothCam has many build-only nested dependencies, while this project needs only its API header. CommonLibSSE-NG's OpenVR submodule is required by its default multi-runtime source build.

```powershell
git submodule update --init external/smoothcam lib/commonlibsse-ng
git -C lib/commonlibsse-ng submodule update --init extern/openvr
.\build.cmd
```

`build.cmd` configures and builds the DLL, builds and runs the state/Core tests, verifies the x64/SKSE exports and DLL dependencies, then writes the deployable DLL and PDB to `dist/SKSE/Plugins`. Use `test.cmd` to build and run only the tests. Both commands accept `-Configuration` and `-BuildDirectory` PowerShell parameters.

Install `dist/SKSE/Plugins/SexlabSceneCamera.dll` under `Data/SKSE/Plugins`. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## POC validation

1. Start the game with SexLab P+, SmoothCam, and this DLL enabled.
2. Start a SexLab scene containing the player.
3. Confirm `SexlabSceneCamera.log` records one fixed scene-anchor position and forward direction.
4. End the scene and confirm normal SmoothCam control remains unchanged.
5. Start an NPC-only scene and confirm no anchor is captured.
6. During an active scene, try a free/photo camera and confirm this plugin does not overwrite it.
7. Inspect `SexlabSceneCamera.log` for event order, scene keys, anchor capture, and restoration.

The compiled DLL proves only that the native interfaces and code agree at build time. The event payload, camera-node behavior, and restoration sequence still require the in-game validation above.
