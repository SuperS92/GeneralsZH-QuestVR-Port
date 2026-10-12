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
