# Sexlab Scene Camera

Native SKSE scene-camera plugin for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin captures a fixed scene anchor from the participants' Pelvis nodes after the camera update boundary. Its position is the Pelvis average; with multiple participants its forward points from that average toward the player Pelvis, and otherwise it falls back to the inverse of the player's horizontal forward. The first valid preset is converted from anchor-relative offsets into a camera pose, and a matching `AnimationChange` reapplies it after recapturing the anchor. Presets can be created, edited, deleted, and reloaded through an SKSE Menu Framework window; changing `right`, `forward`, or `up` requests a live camera preview without saving. Collision, LOS, and clearance selection are not implemented yet.

The POC has no ESP and no dedicated Papyrus script. SexLab P+ integration uses the native SKSE `ModCallbackEvent` dispatcher. P+ currently emits unprefixed `AnimationStart`/`AnimationChange`/`AnimationEnd` compatibility events alongside its documented Papyrus hook API. Because P+ passes `thread_id` as the second `SendModEvent` argument, the native adapter parses it from `strArg` and treats `numArg` only as a compatibility fallback. Generic event names are accepted only when the sender is a quest defined by `SexLab.esm`.

## Requirements

- Skyrim SE/AE (VR is intentionally rejected by this POC)
- SKSE64 and Address Library for the active runtime
- SexLab P+
- SmoothCam 1.7.1
- SKSE Menu Framework 3.4 or later (optional; required only for the in-game preset editor)

For an unambiguous test, disable SexLab's automatic free-camera/TFC option so it does not compete with the POC after the scene starts.

## Build

Initialize only the required submodules. Do not use a recursive submodule update: SmoothCam has many build-only nested dependencies, while this project needs only its API header. CommonLibSSE-NG's OpenVR submodule is required by its default multi-runtime source build.

```powershell
git submodule update --init external/smoothcam lib/commonlibsse-ng
git -C lib/commonlibsse-ng submodule update --init extern/openvr
.\build.cmd
```

`build.cmd` configures and builds the DLL, builds and runs the state/Core tests, verifies the x64/SKSE exports and DLL dependencies, then writes the deployable DLL and PDB to `dist/SKSE/Plugins`. Use `test.cmd` to build and run only the tests. Both commands accept `-Configuration` and `-BuildDirectory` PowerShell parameters.

The anchor marker is controlled by the `SSC_ENABLE_DEBUG_ANCHOR` CMake option, which defaults to `OFF`. Pass `-EnableDebugAnchor` to `build.cmd` when a visible marker is needed.

Install the contents of `dist` under `Data`. This includes the DLL and the initial `Data/SKSE/Plugins/SexlabSceneCamera/presets.json`. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## In-game validation

1. Start the game with SexLab P+, SmoothCam, SKSE Menu Framework 3.4+, and this mod enabled.
2. Start a SexLab scene containing the player.
3. Confirm the initial preset moves the camera and `SexlabSceneCamera.log` records the anchor and applied pose.
4. Open Mod Control Panel, choose Sexlab Scene Camera > Camera Presets, and open the preset editor.
5. Drag `right`, `forward`, and `up`; confirm game time remains active and the scene camera follows without saving.
6. Exercise create, update, cancel, delete, and reload, including the unsaved-change and delete confirmations.
7. Change the animation and confirm the edited offset is reapplied after the anchor recapture.
8. End the scene and confirm camera ownership returns to SmoothCam with no draft preview left active.
9. Remove or disable SKSE Menu Framework and confirm preset loading and the initial camera still work while the editor is absent.

The compiled DLL proves only that the native interfaces and code agree at build time. Menu behavior, input routing, camera-node behavior, and restoration still require the in-game validation above.
