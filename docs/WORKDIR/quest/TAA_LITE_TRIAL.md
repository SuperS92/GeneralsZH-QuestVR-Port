# Full-image TAA Lite — local Test 10

Default off. Keep Android version 1.4.0.9 / 10409 and the published updater.
Select TAA Lite in Graphics, leave other anti-aliasing Off, and fully close the
game and launcher between comparisons. The first trial covers the whole image.

The resolve shader adapts the MIT-licensed [RDR1 Quest implementation](https://github.com/dubrovskiy-yevhen-stakelogic/rdr1-quest/tree/423788baa4959d21d5057243ad31f5c8b40f9f28).
Keep its [original notice](licenses/RDR1-Quest-MIT.txt). No game assets or
Rockstar code are included. RDR Lite normally limits processing to a central
region; this experiment does not claim its performance at full Quest resolution.

## Render boundary

- Capture the actual D3D view/projection after applying the owned eye camera.
  Convert D3D row-vector coordinates through the pinned DXVK negative-height
  viewport, including its half-pixel offset and unchanged [0,1] depth.
- Run after the original world flush and before the pointer. Keep the original
  scene update count, Begin/End_Render calls, menu/HUD, and depth-discard hook.
- Drain pending D3D8 primitive batches with an identical checked viewport set.
  Flush D3D9 commands, then hold the existing interop submission lock, which
  synchronizes the DXVK submission worker before native queue submissions.
- Read the same eye's original depth before its shared surface is overwritten
  by the other eye. Sample directly when supported; otherwise copy its depth
  aspect into one private sampled image. Preserve source depth/stencil contents.
- Use separate ping-pong RGBA8 histories per eye. Camera/depth reprojection,
  five-sample color bounds and motion/subpixel-dependent weights limit history.
  There is no projection jitter, object motion pass, extra scene traversal,
  simulation change or frame-rate change. Animated objects can still ghost.
- Apply only RGB back into the owned eye image; load and preserve original
  alpha. Restore every external image to its reported preferred layout before
  original rendering continues. No external pipeline state enters DXVK caches.
- Reset on first use, camera/projection jumps, recentering, frontend screens,
  skipped frames or a gap above 100 ms. Unsupported images, combined spatial
  AA, failed setup or busy command slots keep original rendering. No per-frame
  queue-idle, fence wait or CPU image readback is added by the filter.

## Cost and evidence

Four 4-byte history images use about 101.5 MiB at 2520 × 2640, before allocation
padding. An unsampleable original depth surface requires an additional private
depth image and copy. GPU timestamps include this copy, image dependencies,
resolve and apply; bridge CPU timings include producer flushing and queue
synchronization. Compare these with whole-app GPU time rather than adding
overlapping measurements. Timestamp sums use the latest completed eye samples.

Host fixtures now cover 128 actual GPU frame cases: RGBA/BGRA, D32/D24S8, direct and
copied depth, separate eyes, exact alpha and reset output, stale-history rejection
and temporal attenuation. An independent row-vector camera oracle checks 1000
world points, including Y orientation, half-pixel offset and depth mapping.
Known-pixel GPU oracles cover orthographic motion and perspective reprojection
with distinct same-eye depths, including the shared surface overwritten between
eyes. Test-only interception checks actual barriers and descriptors against each
image's creation usage; this is a narrow contract check, not full validation.
These fixtures do not validate the D3D8/OpenXR bridge on Quest or its cost.

Hardware gates: same-scene Off/On, stationary and moving head/camera, moving
units, fine fences/trees, shadows/water/effects, pointer/HUD, campaign transitions,
recenter and suspend/resume. Use normal native GPU capture, without detailed
trace mode. Replay and a complete live PC match remain required before release.

## Test 10 feedback and Test 11 correction

Headset logs confirm activation and sustained history use: 6720 eye submissions,
37 resets and three busy skips in the reviewed run. The maintainer reports no
artifacts, but no clear improvement or only a very subtle one. Do not call this
a successful quality/performance result. Logged GPU spans include dependency
waits; they do not establish incremental filter cost without a controlled pair.

Correct the private depth copy to SHADER_READ_ONLY_OPTIMAL. It is a sampled
transfer destination, not a depth attachment; the previous depth-attachment
read-only layout violated [VUID 01210](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html).
The strengthened actual-call test rejects the old Test 10 implementation and
passes the corrected one. This fixes the contract; a visible quality gain from
this correction alone is not established.

Test 10 also used more restrictive history caps than the pinned RDR reference:
8 still frames / 4 moving / 2 subpixel penalty, versus 10 / 5 / 1. Test 11 uses
the reference values, preserving full-image coverage and the same sample count.
It logs reset reasons separately. Check for useful stabilization, blur and
moving-object trails before requesting another performance comparison.

## Test 12: Off / Lite / Full

Test 11 visibly improves edges/fences, but the normal paired headset captures
record mean 60.4 FPS / 13.06 ms app GPU Off and 41.7 FPS / 21.00 ms On at 1.5
scale. GPU clocks match; scene and temperature remain uncontrolled factors.
The existing high-precision cubic shader is retained byte-for-byte as Full.
Lite uses the pinned RDR alternative: one bilinear history lookup instead of
five cubic lookups, with relaxed-precision color moments/clipping/blending.
Depth, reprojection, texture positions and confidence weights stay highp.
Relaxed precision permits driver lowering; it does not guarantee FP16 execution.

Both modes retain full-image coverage, the same five current-color bounds,
10/5/1 caps, depth copy, apply pass, memory and original-alpha preservation.
Only the selected resolve pipeline is allocated. Neither mode introduces
jitter, motion vectors or changes to simulation. Full here means the previous
local filter, not RDR's separate jittered Full algorithm.

The old enabled preference maps to Full; new installs remain Off. Retire
transfer batching and block stale environment/preferences from enabling it.
Keep compact HUD transfer. Compare modes at identical resolution/settings with
HUD transfer held On; the older HUD-Off pair is not the new baseline.
Shader fetch/ALU savings do not establish headset gains. Both modes require
GPU fixture, native, image and normal timing checks before any public release.
