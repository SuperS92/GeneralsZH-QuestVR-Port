# Local test identity

Keep APP_VERSION.json at the current public release during local experiments.
Pass `--test-label "Test 01"` to package-windows.py for a distinct artifact name
and launcher/menu display label. Android versionName and versionCode stay numeric
and unchanged. The released QuestReleasePolicy and APK updater remain unchanged.

Local installers use `adb install -r`, preserving data and the existing signing
key. The next approved public release must raise both actual versions so the
existing updater recognizes it as newer. Omit the test label for publication and
rebuild/review the exact final artifact. Test build names are not public feed assets.

The first local trial adds default-off passthrough behind flat front-end screens.
With it disabled or unavailable, the black background remains. No transparent
sky or gameplay masking is implemented. Check pause/resume, transition to stereo
gameplay, and the existing left-menu recenter behavior on hardware.

## Test 01 feedback and next build

The maintainer reports that front-end passthrough works well. Remove the
experimental label from Tree contours and Passthrough behind flat screens.
Replace the terrain batching comparison instructions with a short description
of its purpose and restart requirement, including the Quest resource override.
Keep the launcher navigation visible on the GeneralsOnline settings page;
the current Online shortcut opens a separate activity without that navigation.
These launcher changes are implemented for Test 02, not present in Test 01.

Gameplay sky passthrough is a separate proposed rendering experiment. It needs
background transparency that preserves opaque world geometry, effects and HUD;
front-end passthrough does not implement this. Begin normal GPU measurements
before adding another graphics experiment. No new crossplay result is inferred.
See [sky and passthrough research](SKY_PASSTHROUGH_RESEARCH.md) for the reviewed
PCVR/GLES approaches and the rendering-only multiplayer boundary.

## Test 02

Add an independent default-off Vehicle track streaming switch and split CPU
buffer measurements. See [trial scope and validation](TRACK_STREAMING_TRIAL.md).
Keep Android version 1.4.0.9 / 10409, with display/artifact label Test 02. The
installed Test 01 and public release remain unchanged until explicitly installed.

## Test 03

Add an independent default-off Color transfer trial. It replaces format-converting
blits with exact-pixel raster output to the acquired swapchains when supported.
See [scope and validation](COLOR_TRANSFER_TRIAL.md). Preserve Test 02's vehicle
track option and retain Android version 1.4.0.9 / 10409 with display label Test 03.
No automatic installation or public upload.

## Test 04

Retire the ineffective Test 03 raster transfer. Add the default-off compact
HUD output trial, retaining pixel width and full output for expanding/front-end
UI. See [scope and validation](HUD_TRANSFER_TRIAL.md). Vehicle track streaming
remains independent. Android version and updater stay at the current public
release; the display/artifact label is Test 04.

## Test 10

Add default-off full-image TAA Lite with camera/depth reprojection. Keep Android
version 1.4.0.9 / 10409 and the public updater unchanged. See [render boundary,
cost and validation](TAA_LITE_TRIAL.md). Test 09 shadow coverage is confirmed on
hardware; its image-transfer grouping has no measured benefit and stays off.

## Test 11

Correct the sampled depth-copy layout, match the pinned RDR Lite history caps,
and separate history reset reasons in diagnostics. Keep full-image coverage,
default Off, Android 1.4.0.9 / 10409 and the existing updater. Stronger host
usage/layout and camera/depth pixel oracles pass; headset quality is pending.

## Test 12

Add Off / Lite / Full temporal filtering. Preserve the Test 11 shader as Full;
Lite uses bilinear history and relaxed color precision, with precise camera
and depth math. Full-image coverage and the depth-copy/alpha contract remain.
Retire ineffective transfer batching; keep HUD transfer. Android remains
1.4.0.9 / 10409 and the updater is unchanged. Hardware gain is unmeasured.


## Test 13

Bundle centered-area temporal filtering, smaller depth transfers, grouped
dependencies, reset/rejection shortcuts and sparse timing into the new Lite.
Rename the previous Lite to Medium; keep Full unchanged. Stable saved IDs
preserve existing choices. Graphics-only changes; Android remains 1.4.0.9.
No new render pacing, simulation, numeric or network changes.


## Test 14

Add one default-off Lite transfers switch for sampled D24S8 depth and native
RGBA copy with separate R8 confidence. Keep the Test 13 Lite when Off and retain
Medium/Full. Automatic capability/allocation fallbacks keep the existing path.
Android stays 1.4.0.9. Hardware performance and crossplay are not certified by
compilation or the host output comparisons.
