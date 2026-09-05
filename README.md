# Sexlab Scene Camera

Native SKSE scene-camera plugin for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin captures a fixed scene anchor from the participants' Pelvis nodes after the camera update boundary. Its position is the Pelvis average, and its forward is the inverse of the player Pelvis node's horizontal world forward, with actor yaw used only as a degenerate-axis fallback. This makes preset yaw `0` the player's front side and yaw `±180` the back side. Presets define screen-relative `Pan Right`/`Pan Up` framing, a `yaw`/`pitch`/`distance` orbit, and an FOV offset relative to the user's normal third-person FOV. Each preset is evaluated against the participants' face, chest, and waist points, and the first preset that can show every participant is selected. The Camera Presets dashboard reports each preset as usable, blocked, or not evaluated without changing the camera. `Preview & edit` then pauses game time and starts the live camera editor, including for a preset blocked in the current scene.

The POC has no ESP and no dedicated Papyrus script. SexLab P+ integration uses the native SKSE `ModCallbackEvent` dispatcher. P+ currently emits unprefixed `AnimationStart`/`AnimationChange`/`AnimationEnd` compatibility events alongside its documented Papyrus hook API. Because P+ passes `thread_id` as the second `SendModEvent` argument, the native adapter parses it from `strArg` and treats `numArg` only as a compatibility fallback. Generic event names are accepted only when the sender is a quest defined by `SexLab.esm`.

## Requirements

- Skyrim SE/AE (VR is intentionally rejected by this POC)
- SKSE64 and Address Library for the active runtime
- SexLab P+
- SmoothCam 1.7.1
- SKSE Menu Framework 3.4 or later (optional; required only for the in-game preset editor)

Improved Camera is unsupported because it is known to compete for the same camera path. If `ImprovedCameraSE.dll` is loaded, Sexlab Scene Camera refuses to acquire camera control and reports the reason in the preset page.

### Recommended configuration

To prevent NPCs from fading when the camera gets too close, enable the following option in `SSEDisplayTweaks.ini`:

```ini
[Miscellaneous]
DisableActorFade=true
```

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

Visibility rays are also disabled in distribution builds. Pass `-EnableVisibilityDebug` to `build.cmd` to enable their HUD layer and detailed per-point logging. Its status and occluded-segment control are on the Camera Presets page.

Install the contents of `dist` under `Data`. This includes the DLL and the initial `Data/SKSE/Plugins/SexlabSceneCamera/presets.json`. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## In-game validation

1. Start the game with SexLab P+, SmoothCam, SKSE Menu Framework 3.4+, and this mod enabled.
2. Start a SexLab scene containing the player.
3. Confirm the scene toolbar remains visible, names the preset currently shown by the camera, shows `Press [F8] to edit this preset`, and follows every `A` / `D` preset change without taking mouse input.
4. Select a non-default preset with `A` / `D`, press the displayed edit hotkey, edit and save it, then press the same key to close. Confirm the camera does not return to the default preset and the edited preset remains active even when its visibility status is blocked. Also confirm unsaved changes still produce the normal close prompt.
5. In a debug-anchor build, confirm the arrow points opposite the player Pelvis front for both front-facing and back-facing animation poses. Confirm `SexlabSceneCamera.log` records the Pelvis and actor direction samples, anchor, and applied pose.
6. Open Mod Control Panel and choose Sexlab Scene Camera > Camera Presets. Confirm the dashboard lists all presets with usable, blocked, or not-evaluated status and that selecting a row does not change the camera.
7. Choose a preset and select `Preview & edit selected`. Confirm game time pauses only now, then drag `Pan Right`, `Pan Up`, `yaw`, `pitch`, `distance`, and `FOV Offset`; confirm the scene camera and visibility status follow the edits.
8. Change the edit hotkey on the Camera Presets page and confirm preview actions remain unavailable until a key is assigned or the change is cancelled. Close and reopen the game, and confirm the toolbar and open/close action use the saved assignment without leaking the key hold or release into scene controls. Then exercise create, update, cancel, delete, and reload, including the unsaved-change and delete confirmations.
9. Confirm scene time and animation remain paused until the editor closes, then change the animation and confirm the saved framing offset and orbit are reapplied after anchor recapture.
10. End a scene with the editor closed and confirm the toolbar disappears, the FOV offset is cleared, and ownership returns to SmoothCam immediately.
11. Remove or disable SKSE Menu Framework and confirm preset loading and the initial camera still work while the editor and scene toolbar are absent.
12. Load the game with Improved Camera enabled and confirm the preset page reports it as unsupported without acquiring camera control.
13. In an open area, confirm every participant has at least one green face, chest, or waist ray and that the first usable preset becomes active.
14. Put one participant behind a wall and confirm affected rays stop at red hit markers, the preset is rejected, and another usable preset is selected when available.
15. In a visibility-debug build, preview a blocked preset and toggle occluded segments; confirm gray hit-to-target paths and yellow hit normals match the saved log results. Confirm the evaluation log counts the physical ray queries used while passing through character collision.
16. Make every preset unusable, then change the animation; confirm the camera returns to SmoothCam, the scene toolbar reports that no preset is active, the edit hotkey is not consumed, and the dashboard can still start an editing preview.

The compiled DLL proves only that the native interfaces and code agree at build time. Menu behavior, input routing, camera-node behavior, and restoration still require the in-game validation above.
