# Runtime hidden-area mask trial

An independent default-off Quest option uses `XR_KHR_visibility_mask` when the
installed runtime provides a hidden triangle mesh. No lens border is hardcoded.
Restart the game and launcher after changing the option. Temporal AA Off and
Lite are supported; Medium/Full retain their original full-image rendering.

Project each eye's z=-1 mask vertices through its asymmetric FOV, matching the
actual bound D3D projection and the pinned DXVK half-pixel convention. Build
32-pixel blocks only when their entire 16-pixel guard lies inside a runtime
hidden triangle. Keep the centered 70% rectangle plus the Lite gather/history
border untouched. Conservative omissions are allowed; guessed coverage is not.

Before that eye's world draw, clear only those blocks' depth to zero on the
owned single-sample D24S8 eye surface. Do not clear color or stencil, change
render states or skip object updates. Geometry that honors the normal depth
test can avoid hidden fragment work. Passes that ignore or overwrite depth may
still run; this does not guarantee a GPU improvement. The final color transfer,
HUD, simulation, visibility lists, RNG and network code are unchanged.

Masks are cached per eye/FOV and refreshed on runtime mask-change events.
Check runtime counts, indices, finite coordinates, bounded preparation work,
projection, target identity, viewport, depth format and sample count. Missing
extensions/entry points, empty or malformed masks and unsupported views retain
the original world rendering. Optional extension failure retries without that
extension. No custom DXVK changes are required.

## Validation

The host fixture independently checks every guarded pixel against triangle
area coordinates at odd sizes, both windings/eyes, asymmetric projections,
invalid indices/FOV/coordinates and pathological preparation work. Eight Vulkan
render/readback cases demonstrate depth rejection only in planned blocks,
exact visible color/alpha and preservation of every stencil byte. This tests
the planner and equivalent GPU operation, not the complete D3D8/DXVK bridge.

Native builds, actual runtime mask availability, Quest edge quality, GPU time,
water/shadows/effects and lifecycle are separate gates. The prior observation
of peripheral geometry popping during head turns is unresolved and predates
this trial; its presence with the GPU bundle disabled has not been tested.
Keep this option experimental and do not certify crossplay without the
existing replay/completed PC-match checks. Android version/updater stay stable.

Primary references: [mask coordinate space](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrVisibilityMaskKHR.html)
and [runtime query contract](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrGetVisibilityMaskKHR.html).
