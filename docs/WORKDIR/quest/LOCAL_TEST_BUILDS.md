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
