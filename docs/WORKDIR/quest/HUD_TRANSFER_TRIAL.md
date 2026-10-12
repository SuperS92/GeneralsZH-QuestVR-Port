# Compact HUD output trial

Local Test 04 adds **HUD transfer (experimental)**, default off. Fully restart
the game and launcher after changing it. Keep vehicle track streaming independent
and retain Android version 1.4.0.9 / 10409 with the existing APK updater.

When enabled, create one additional half-height OpenXR menu swapchain at the
full UI pixel width. Its images hold the bottom half of the original menu
canvas, without scaling. Normal wrist HUD rendering uses this output only
when the visible crop fits with at least 64 source pixels above it. The whole
chosen destination is populated every frame, including the guard, rather than
leaving undefined texels outside a partial copy. Left/right/bottom image edges
retain the original source edges.

Expanded descriptions, promotion/radial windows, keyboard, pause menus,
main menu and flat campaign/movie sequences use the existing full output
whenever they no longer fit or are presented as full-screen UI. The compositor
subimage offset changes by the source-tail offset; its size, pose, pixel width
and original cursor coordinates stay unchanged. The displayed bottom is fixed.
No per-frame swapchain recreation, readback or queue-idle is introduced. If
the optional swapchain cannot be allocated/enumerated, retain the full path.
Additional swapchain memory is allocated only when enabled; record device image
counts before assessing its memory cost. Source UI rendering itself is unchanged.

The helper handles both nearest BGRA-to-RGBA blits and compatible-format copies.
Source/destination barriers, acquire/wait/release, command-buffer fences and
queue-drained teardown remain owned by the existing bootstrap. The successful
source-boundary and host checks do not certify headset or online behavior.

Host tests use the actual GPU transfer helper and an independent CPU oracle.
They verify every destination texel and the submitted visible subimage against
full-canvas output: both formats, alpha/orientation, odd extents, crop thresholds,
changing content, two slots in flight, resource-unavailable and expanded/frontend
fallback. Exhaustive rectangle checks preserve original crop coordinates and
the fixed bottom. Runtime compositor filtering must still be checked visually,
especially near HUD boundaries and when moving the wrist far from the headset.

Performance metrics log compact/full frame counts and actual/original copied
pixels every 120 frames. Startup reports requested/active state and dimensions.
Compare 90-second normal GPU captures in the same saved skirmish, with vehicle
track streaming on, 1.5 render scale, shadows on and AA off. Review FPS and app
GPU timing as well as the bridge timestamp; the bridge is already part of app
GPU work. No benefit is promised from reducing pixel count alone. Detailed
driver profiling remains blocked. Before public promotion, check visual/lifecycle
behavior and complete a PC crossplay match with accepted options enabled.

The Test 03 raster transfer is retired after its pair showed no measurable
FPS or app-GPU gain. Its old preference is ignored and its code/fixtures remain
recoverable in local Git history. No simulation, RNG, CRC or transport changes.

Reference: [OpenXR subimages and compositor filtering](https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html#XrSwapchainSubImage).
