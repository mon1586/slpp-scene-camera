# Sexlab Scene Camera

Native SKSE scene-camera plugin for SexLab P+ and SmoothCam.

When a player-involved SexLab P+ scene reaches `AnimationStart`, the plugin uses the smoothed midpoint between the player's pelvis and upper spine as the scene anchor and follows it throughout the active scene. Its forward is the inverse of the Actor's horizontal forward, making preset yaw `0` the player's front side and yaw `±180` the back side. Presets define screen-relative `Pan Right`/`Pan Up` framing, a `yaw`/`pitch`/`distance` orbit, and an FOV offset relative to the user's normal third-person FOV. Each preset is evaluated using five rays to the anchor: center visibility and at least three visible corners are required. The first usable preset is selected at scene start. The Camera Presets dashboard reports each preset as usable, blocked, or not evaluated without changing the camera. `Preview & edit` then pauses game time and starts the live camera editor, including for a preset blocked in the current scene.

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

The Camera Presets page includes a persistent debug mode. While enabled, the plugin leaves camera control with SmoothCam and overlays the selected preset position, visibility rays, and scene anchor. Use `A` / `D` to cycle through currently usable presets without moving the camera.

During an active scene, both normal and Debug modes reevaluate five LOS rays per saved preset every 0.5 seconds. They start at the center and four corners of a fixed 32-by-18 rectangle centered on the candidate camera, aligned with its right/up axes, and converge on the same smoothed anchor. Corner labels mark the camera-side origins; the rays narrow toward the body. Distance and FOV do not resize the rectangle. Characters, including the player, are ignored; walls and furniture still obstruct rays. The HUD separates center LOS from visible corners (0–4), and reports batch/LOS time, average and maximum batch time, and actual physics query count (including character traversal). The results update A/D eligibility: a blocked center, two or more blocked corners, or any unavailable ray excludes a preset. The active camera remains selected even if every candidate becomes blocked; recovered candidates return to A/D choices without an automatic switch. `Anchor LOS benchmark` summaries appear in `SexlabSceneCamera.log` on the first sample and every ten samples; timings exclude logging and HUD drawing. Compare a fixed preset count in an open area and near furniture for 30 seconds each, toggling Debug mode between runs to reset statistics.

Install the contents of `dist` under `Data`. The build distributes only the DLL and PDB; `presets.json` is user-owned data and is neither supplied nor overwritten by the build. Create the first preset from the Camera Presets page. The matching log is written to the normal SKSE log directory as `SexlabSceneCamera.log`.

## In-game validation

The [specification coverage checklist](docs/spec-coverage.md) maps states and events, decision rules, invariants, and sequence cases to the design documents, and tracks unresolved specification gaps separately from test results.

1. Start the game with SexLab P+, SmoothCam, SKSE Menu Framework 3.4+, and this mod enabled.
2. Start a SexLab scene containing the player, open the Camera Presets page, and create and save the first preset if none exists yet.
3. Confirm the scene toolbar remains visible, names the preset currently shown by the camera, shows `Press [F8] to edit this preset`, and follows every `A` / `D` preset change without taking mouse input.
4. Select a non-default preset with `A` / `D`, press the displayed edit hotkey, edit and save it, then press the same key to close. Confirm the camera does not return to the default preset and the edited preset remains active even when its visibility status is blocked. Also confirm unsaved changes still produce the normal close prompt.
5. Enable Debug mode and confirm camera control returns to SmoothCam. The orange `Torso target` marks the midpoint between the player's waist and chest; the cyan `Anchor` follows it smoothly and points opposite the Actor's horizontal front. Compare both markers during movement and posture changes, including lying down: fast jitter should be reduced while slow motion remains. For a stationary target, the gap halves every 0.15 seconds. All five LOS rays converge on the cyan anchor.
6. Open Mod Control Panel and choose Sexlab Scene Camera > Camera Presets. Confirm the dashboard lists all presets with usable, blocked, or not-evaluated status and that selecting a row does not change the camera.
7. Choose a preset and select `Preview & edit selected`. Confirm game time pauses only now, then drag `Pan Right`, `Pan Up`, `yaw`, `pitch`, `distance`, and `FOV Offset`; confirm the scene camera and visibility status follow the edits.
8. Change the edit hotkey on the Camera Presets page and confirm preview actions remain unavailable until a key is assigned or the change is cancelled. Close and reopen the game, and confirm the toolbar and open/close action use the saved assignment without leaking the key hold or release into scene controls. Then exercise create, update, cancel, delete, and reload, including the unsaved-change and delete confirmations.
9. Confirm scene time and animation remain paused until the editor closes, then change the animation and confirm the saved framing offset and orbit keep following the body center while preset visibility is reevaluated.
10. End a scene with the editor closed and confirm the toolbar disappears, the FOV offset is cleared, and ownership returns to SmoothCam immediately.
11. Remove or disable SKSE Menu Framework and confirm preset loading and the initial camera still work while the editor and scene toolbar are absent.
12. Load the game with Improved Camera enabled and confirm the preset page reports it as unsupported without acquiring camera control.
13. In an open area, confirm all five camera-side rays reach the anchor and the first usable preset becomes active.
14. Obstruct the center ray or at least two corner rays with a wall or furniture. Confirm the preset becomes blocked at the next evaluation and A/D skips it. The current preset must remain active until a manual selection.
15. While Debug mode is enabled, use `A` / `D` to cycle through usable presets without moving the camera. Toggle occluded segments and confirm gray hit-to-target paths and yellow hit normals match the saved ray results. Confirm the evaluation log counts the physical ray queries used while passing through character collision.
16. Make every preset unusable, then change the animation; confirm the current camera remains active and A/D does not change it. Clear the obstruction and confirm recovered presets return to A/D choices without an automatic switch.

17. Open a new preset preview and close without saving. Confirm the pre-editor preset returns even if blocked; with no pre-editor selection, SmoothCam remains in control. A deleted pre-editor preset must not cause another preset to be selected automatically.
18. While a scene is active, switch Debug mode off during a temporary camera-control refusal. Confirm recovery succeeds after the refusal clears, with at most three attempts spaced at least 0.5 seconds apart and within two seconds. If refusal persists, confirm the retry stops with a message, including while paused. Toggle Debug mode on/off to retry explicitly.

The compiled DLL proves only that the native interfaces and code agree at build time. Menu behavior, input routing, camera-node behavior, and restoration still require the in-game validation above.
