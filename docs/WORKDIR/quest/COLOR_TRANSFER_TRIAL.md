# Exact-pixel color transfer trial

Historical Test 03 experiment, retired in Test 04. The stationary pair showed
48 FPS with both paths and essentially unchanged app GPU time; raster transfer
was slightly slower. Remove its active engine path and launcher control; ignore
the saved preference. Code and fixtures remain recoverable in Git history.
Continue with [compact HUD output](HUD_TRANSFER_TRIAL.md).

Local Test 03 adds **Color transfer (experimental)**, default off. Fully close
the game and launcher after changing it. Android version 1.4.0.9 / 10409 and
the published APK updater stay unchanged. Vehicle track streaming remains an
independent option.

The existing bridge converts completed DXVK BGRA8 eye and menu images into
OpenXR RGBA8 swapchains with nearest blits. The trial instead samples each
source texel in a fullscreen triangle directly into the acquired color
attachment. Both paths retain all pixels, dimensions, alpha and UNORM color
space. No image scaling, image filtering, blending, temporal history or extra
intermediate color image is introduced. There are three immutable source
descriptors: left eye, right eye and UI. Swapchain framebuffers are separate
for each destination image and eye layer.

Only sampled BGRA8 UNORM sources and RGBA8 UNORM destinations qualify. Missing
resources, unsupported usage/formats or changed source identities use the
existing transfer. Spatial AA retains its original filtered-eye transfer;
the UI trial may remain active. Raw-eye diagnostics retain their source
layout restoration. DXVK flush, submission-queue ownership, acquire/wait/release,
source restoration, command-buffer fences and teardown draining remain in place.
No per-frame queue-idle or descriptor rewrite is added.

The host GPU fixture runs the actual class against reference Vulkan blits and
CPU expectations. It checks every byte in 48 channel transfers, including
distinct eye/HUD sizes, alpha, orientation, changing content, two submissions
in flight, slot reuse, unsupported-input fallback and repeated teardown. This
establishes fixture pixel correctness, not Quest driver performance or crossplay.

On hardware, keep Vehicle track streaming on and other settings fixed. Compare
the same saved skirmish with Color transfer off, then on after a full restart.
Use normal 90-second GPU captures without video or detailed driver profiling.
Review runtime FPS, App GPU time and copy_gpu; copy_gpu is part of App GPU time
and must not be added to it. Startup reports requested/active transfer state.
Check both eyes, colors, HUD text and expanding tooltips, campaign flat video,
keyboard and pause/resume. Test the demanding USA mission if the first pair
shows a benefit. Complete a PC match before public promotion; that gate is
currently deferred at the maintainer's request.

72 Hz allows about 13.9 ms per new image, with overhead/headroom also needed.
The existing engine and network pacing remain upstream-owned. This transfer
trial neither changes simulation rate nor unlocks rendering above its current
limit. Stable lower-cost frames are the immediate target; higher render cadence
requires a separate pacing review without changing online simulation behavior.

References: [Vulkan nearest blits](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html),
[Vulkan texel fetch](https://docs.vulkan.org/spec/latest/chapters/textures.html).
