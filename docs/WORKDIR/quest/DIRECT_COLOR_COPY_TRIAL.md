# Direct color copy trial

Local Test 08 adds **Direct image copy (experimental)**, default off, alongside
retiring the unsuccessful terrain-view control. Keep 1.5 scale, shadows, track
streaming, compact HUD, neutral fog and completed-eye depth options unchanged
in both captures. Disable AA and restart the game and launcher after changing
the trial. Android version remains 1.4.0.9 / 10409; the APK updater is unchanged.

The Quest runtime advertises RGBA8 UNORM but no BGRA8 UNORM. Existing owned DXVK
eye/menu textures use BGRA8 and Vulkan converts them into RGBA8 by blitting.
The new trial uses DXVK's existing D3D9 A8B8G8R8 mapping for only these owned
presentation textures. A Quest-only bridge overlay reports its four-byte stride.
Retail textures, geometry, shaders, simulation, random, CRC and network code,
dependency pins and numeric policies are unchanged. No resolution reduction.

Require the actual runtime RGBA8 format, native render/sampling/transfer support,
single sampling and AA off. Validate the actual Vulkan image format/extent.
If either eye's allocation or interop fails, release the attempted pair and
retry the original BGRA pair together. AA or MSAA retains the original targets.
The menu uses the same format as the verified eye pair. Full-color GPU targets
still carry alpha; neutral fog accepts both equivalent four-channel formats.

Keep the original transfer layout barriers, fences, queue ownership, acquisition,
source pixels, extents, eye array layer, HUD crop and submission. The original
copy/blit commands move into an owned helper. Matching RGBA formats choose the
existing copy path; log actual source/destination formats and command choice.
The direct path is different from retired Test 03's raster conversion shader.
No store-op or bandwidth gain is promised from successful format negotiation.

The actual eye-transfer helper passes 32 host Vulkan cases against CPU pixels
and the HUD transfer helper, including both destination eye layers, changing
content/alpha, guard/crop/full modes and two slots in flight. Corresponding BGRA
blits and RGBA copies produce identical logical output bytes. Candidate gates
also pass. These checks do not certify the D3D9 format's complete game renderer,
Quest color/alpha presentation, device performance or crossplay.

Follow with both native profiles, matching patched backend hashes, exact APK
checks, and Quest review of colors, alpha, both eyes, intro/campaign movies,
shadows, water, cutouts, HUD and pause. Use paired normal 90-second captures.
Check active/source format and copy command logs before attributing a result.
A replay and a complete live PC match remain gates before public promotion.
Any release must carry corresponding sources for the added bridge overlay.
