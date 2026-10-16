# Tabletop comfort and graphics defaults

Hold both grips and flick the right stick up/down to tilt the battlefield in
15-degree steps. Return the stick to neutral between steps. Flick left while
holding both grips to reset to zero. The angle is saved in `QuestVR.ini` and
defaults to zero. It applies only in Tabletop mode. The existing two-grip
right-stick **click** still changes view mode. Pinch scaling is suspended once
a tilt gesture starts, until the grips are released.

One rigid room-to-world transform rotates both eyes and controller rays around
a shared captured head center. Changing the angle preserves that center's world
position; ordinary tracking then uses a fixed pivot. Recenter/reference-space
changes refresh the pivot when they take effect. HUD and flat-screen geometry,
simulation, object updates, network messages and numeric flags are unchanged.

The right stick also generates rate-limited wheel steps while pointing at flat
menus. Keyboard/radial ownership and ray loss reset the repeat state. It does
not generate world commands or replace the normal in-game camera controls.

After the completed PC-hosted Quest victory on the preceding build, accepted
render paths become automatic Quest defaults: track streaming, compact HUD
transfer, neutral shroud pass, completed-eye depth discard, direct color copy,
Lite transfers, terrain batching and corrected shadow viewport coverage. Keep
native capability/format/lifetime/mode fallbacks and ordinary Android behavior.
Retired stored switches cannot disable these accepted paths.

Retain compact Lite history and reduced HUD-copy regions for their measured
storage/copy savings; they did not demonstrate additional FPS gain. Remove the
unsupported mask trial from the launcher and keep it inactive. TAA Off, Lite,
Medium and Full, visible quality choices and optional performance logging remain.
The public Android version and APK updater do not change for this local trial.

Host tests cover geometry, stereo distance, renderer/ray composition, pivot
changes, identity at zero and gesture ownership. The maintainer subsequently
reported that the Test 17 build works perfectly on the headset. This feedback
does not establish an additional completed online match or cover every lifecycle
case. Earlier peripheral geometry popping remains a separate open observation.
