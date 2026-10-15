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

Host fixtures cover 96 actual GPU frame cases: RGBA/BGRA, D32/D24S8, direct and
copied depth, separate eyes, exact alpha and reset output, stale-history rejection
and temporal attenuation. An independent row-vector camera oracle checks 1000
world points, including Y orientation, half-pixel offset and depth mapping.
These fixtures do not validate the D3D8/OpenXR bridge on Quest or its cost.

Hardware gates: same-scene Off/On, stationary and moving head/camera, moving
units, fine fences/trees, shadows/water/effects, pointer/HUD, campaign transitions,
recenter and suspend/resume. Use normal native GPU capture, without detailed
trace mode. Replay and a complete live PC match remain required before release.
