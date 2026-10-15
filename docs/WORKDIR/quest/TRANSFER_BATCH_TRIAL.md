# Batched image transfer and shadow coverage trials

Local Test 09 keeps Android version 1.4.0.9 and the existing APK updater.
Both new options default off and need a complete game/launcher restart.

## Batched image transfer

`quest_transfer_batch` / `GX_QUEST_TRANSFER_BATCH` groups the two completed eye
images and optional HUD copy into one transfer interval. Keep the original DXVK
flush, same submission queue, swapchain acquisition, per-slot fences and final
release order. No extra submissions, waits, allocations, shader or format change.

Consolidate the existing per-image barriers into two barrier calls. Preserve
the ALL_COMMANDS/TRANSFER dependency scopes, layouts, access masks and complete
image subresource ranges; include transfer writers for all sources. Restore each
source's queried layout and put both destinations in COLOR_ATTACHMENT_OPTIMAL.
Each eye still writes its own array layer. Every compact HUD pixel and guard
row is copied at the original scale. This is independent of Test 08's RGBA mode.

Only the unfiltered stereo path qualifies. Filtered AA, raw-eye capture, flat
screens, invalid crops/layouts and aliased or missing handles keep the original
commands. Log actual batched and legacy frames. A preference alone is not proof
that the new path ran. Whether fewer dependency calls reduce GPU time on Quest
is an open measurement, not a promised performance gain.

The actual helper passes 32 host GPU comparisons against sequential transfers
and independent input pixels. Cover distinct eye content, both output layers,
BGRA blit/RGBA copy, compact/full/no HUD, alpha/guard rows, changing data and two
independent slots submitted before waiting. Invalid inputs record no commands.
This fixture does not replace the Quest runtime or exercise its compositor.

## Shadow edges

`quest_shadow_coverage` / `GX_QUEST_SHADOW_COVERAGE` changes only the final
stencil-shadow tint rectangle. The inherited coordinates use TacticalView's
flat UI canvas. A taller eye render target may therefore contain stencil
shadow pixels outside that rectangle, producing a straight premature cutoff.
This is a source-based hypothesis for the maintainer's reported edge line;
headset confirmation is still required.

Read the actual device viewport only while an explicitly scoped world eye is
active, the bound color surface is the bootstrap-owned eye surface, and the
renderer has not inverted culling for a reflection. Require valid surface and
viewport extents. Unknown bindings and all other passes retain the old quad.
Cover the entire verified viewport with the same XYZRHW quad, color and two
triangles. Do not change any volume geometry, visibility tests, shadow updates,
INCR/DECRSAT stencil passes, depth state, blend state or shadow resolution.

The host fixture checks 36,864 independent viewport plans plus overflow cases.
It is not a native stencil-pixel test. The correction can shade edge pixels
that were previously omitted; do not present it as a GPU optimization.

## Headset validation

First check the shadow option Off/On near the reported straight cutoff, with
image transfer grouping held off. Inspect both eyes, moving shadows, aircraft,
water and the edge of the view. Then keep shadow coverage fixed for the paired
90-second transfer Off/On captures. Keep scale 1.5, shadows on, AA off and prior
accepted experiments fixed. Compare runtime App GPU time and transfer GPU time
even when FPS reaches the existing 60 cap. Native stages include CPU waits;
hardware counters include compositor work. No OVR app or detailed trace needed.

Neither option changes game logic, simulation rate, RNG, transport, CRC policy,
compiler numeric flags or the 60 FPS limit. A replay and complete live PC match
remain separate requirements before publication. No new public release is made.
