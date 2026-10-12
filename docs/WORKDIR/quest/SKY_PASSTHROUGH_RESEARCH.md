# Sky backgrounds and gameplay passthrough

Reviewed 12 October 2026. Research only: no runtime, game settings, assets or
renderer code changed. Public GitHub default-branch commits still match the
earlier reviewed snapshots. These projects are design references, not additional
donors for the sealed offline v0.44 import.

## Gonzorro sky pictures

Repository: Gonzorro/GeneralsVR, feature/openxr-vr,
`60eb8e110387b0bc3d5fffd7200b7a1f4ff01976`.

[W3DDisplay.cpp, lines 2089–2554 and 2690–2717](https://github.com/Gonzorro/GeneralsVR/blob/60eb8e110387b0bc3d5fffd7200b7a1f4ff01976/GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp#L2089)
enumerates up to eight PNG/BMP files in a Skies folder beside the executable.
The current implementation reads 8-bit PNG and 24-bit uncompressed BMP, not HDR
floating-point images. It wraps a seamless image onto an inward dome with 12
rings and 24 segments, repeats the texture, centers it on the eye and draws it
before the battlefield with depth testing/writes disabled. This is decorative
background art; it does not supply environment lighting.

Drawing goes through DX8Wrapper. The comments document earlier direct-device
state changes that broke an eye and stencil shadows. Any independent Quest
version must preserve wrapper state and both eyes. Use a Quest-only background
resource, check each image's actual license and keep assets outside imported game
archives and GeneralsOnline data. An image adds GPU work rather than a measured
performance benefit. Do not promise a negligible cost before profiling.

## XR tabletop passthrough

Repository: Cesarus85/Generals-Zero-Hour-XR, main,
`50845bb3f618d04b0ad6bdc392d007b9a8ad0c88`.

[XrHello.cpp](https://github.com/Cesarus85/Generals-Zero-Hour-XR/blob/50845bb3f618d04b0ad6bdc392d007b9a8ad0c88/GeneralsMD/Code/Main/XrHello.cpp#L1083)
uses XR_FB_passthrough reconstruction below the projection layer. Eye buffers
clear with zero alpha; the projection blends with source alpha. Transparent
pixels outside the board reveal the room. Environment blend mode remains OPAQUE.
The ground observer explicitly uses an opaque horizon and suppresses passthrough;
its free camera is not evidence that our requested gameplay sky already works.

[gles_pipeline.cpp](https://github.com/Cesarus85/Generals-Zero-Hour-XR/blob/50845bb3f618d04b0ad6bdc392d007b9a8ad0c88/Core/Libraries/Source/d3d8gles/src/gles_pipeline.cpp#L2381)
and [XRStereoShader.h](https://github.com/Cesarus85/Generals-Zero-Hour-XR/blob/50845bb3f618d04b0ad6bdc392d007b9a8ad0c88/Core/Libraries/Source/d3d8gles/include/XRStereoShader.h)
maintain coverage separately from material alpha: opaque fragments become fully
covered after the original alpha test, blended draws accumulate coverage, and
shadow fills preserve destination alpha. Board clipping is specific to tabletop.
These GLES changes cannot be inserted unchanged into our DXVK/Vulkan renderer.

## Quest implementation direction and multiplayer boundary

Our eye targets already have an alpha channel, but the existing projection is
opaque. Clearing alpha alone would not establish correct world coverage.
Audit opaque/cutout materials, water, additive particles, fog and stencil shadows
before enabling source-alpha composition. Keep the laser, HUD, loading screens
and flat campaign presentation correct. A depth-only mask is insufficient for
effects that do not write depth; color-keying the sky could erase real objects.
The existing Gentle filter preserves source alpha, which is useful but does not
solve coverage semantics.

Prefer an optional, default-off Quest graphics setting, reusing the existing
passthrough lifecycle with opaque fallback. No sky art and passthrough should
draw over one another. Measure app GPU and compositor cost separately.

Expected direct CRC risk is low if changes only affect rendering. Do not change
simulation ticks, RNG, network transport, CRC policy, gameplay math, game archives
or synchronized data. Do not add scene updates for sky or coverage passes, or
change the existing once-per-frame scene advancement. Visual clipping must never
alter gameplay visibility or target selection. Source review cannot certify live
crossplay: validate replay checkpoints and a complete PC match before claiming
runtime compatibility for an implemented gameplay variant.

Next: finish the normal GPU baseline, then select a graphics experiment. Neither
sky pictures nor gameplay passthrough has been implemented by this research.
