# Sexlab Scene Camera

Native SKSE camera-controller proof of concept for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin captures a fixed scene anchor from the participants' Pelvis nodes after the camera update boundary. Its position is the Pelvis average; with multiple participants its forward points from that average toward the player Pelvis, and otherwise it falls back to the inverse of the player's horizontal forward. A matching `AnimationChange` schedules an anchor recapture one second later while preserving the previous anchor during the wait. Camera presets and collision/raycast selection are not implemented yet, so the plugin deliberately leaves SmoothCam in control rather than placing a camera inside the scene anchor. Development builds also place a direction arrow at the captured anchor. A matching `AnimationEnding` or `AnimationEnd` discards the anchor and marker; lifecycle resets, invalid participants, and a watchdog provide additional fail-safe exits.

The POC has no ESP and no dedicated Papyrus script. SexLab P+ integration uses the native SKSE `ModCallbackEvent` dispatcher. P+ currently emits unprefixed `AnimationStart`/`AnimationChange`/`AnimationEnd` compatibility events alongside its documented Papyrus hook API. Because P+ passes `thread_id` as the second `SendModEvent` argument, the native adapter parses it from `strArg` and treats `numArg` only as a compatibility fallback. Generic event names are accepted only when the sender is a quest defined by `SexLab.esm`.

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

The anchor marker is enabled by the `SSC_ENABLE_DEBUG_ANCHOR` CMake option, which defaults to `ON`. Configure with `-DSSC_ENABLE_DEBUG_ANCHOR=OFF` when a marker-free build is needed.

Install `dist/SKSE/Plugins/SexlabSceneCamera.dll` under `Data/SKSE/Plugins`. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## POC validation

1. Start the game with SexLab P+, SmoothCam, and this DLL enabled.
2. Start a SexLab scene containing the player.
3. Confirm a direction arrow appears at the fixed scene anchor and `SexlabSceneCamera.log` records its position and forward direction.
4. Change the animation with the P+ hotkey and confirm the log schedules a recapture, then the marker moves to the newly fixed anchor after about one second.
5. End the scene and confirm the marker disappears and normal SmoothCam control remains unchanged.
6. While the marker is visible, save and reload once; confirm the old marker is not persisted or duplicated after the lifecycle reset.
7. Start an NPC-only scene and confirm no anchor is captured.
8. During an active scene, try a free/photo camera and confirm this plugin does not overwrite it.
9. Inspect `SexlabSceneCamera.log` for event order, scene keys, anchor capture, and restoration.

The compiled DLL proves only that the native interfaces and code agree at build time. The event payload, camera-node behavior, and restoration sequence still require the in-game validation above.
