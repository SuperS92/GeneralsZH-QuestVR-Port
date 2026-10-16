# GPU savings trial

One default-off Quest graphics option bundles three render-only experiments.
Restart both the game and launcher after changing it. Android version, the APK
updater, simulation, networking and compiler numeric flags stay unchanged.

- **Lite dependencies:** narrow internal history/color-copy stage scopes to
  their known producers and consumers. DXVK producer/consumer boundaries retain
  all-command scopes, the producer flush and queue ownership. Shared eye depth
  is still consumed before the next eye overwrites it.
- **Lite storage:** with Lite transfers active, allocate color/confidence
  histories for the centered rectangle only. Preserve the same full-eye source
  positions, gather footprint, inset and history limits. Local texture coordinates
  can round RGB by one UNORM8 step; alpha and peripheral pixels remain exact.
  Medium, Full and the disabled bundle retain their existing shaders.
- **HUD work:** clear and copy only the displayed compact HUD plus a 64-row top
  guard. Keep the original source canvas, destination subimage coordinates,
  panel size and pointer mapping. Expanded HUD, radial/keyboard/front-end UI and
  unavailable compact resources use the full-region fallback. All widgets still
  draw every frame. Reused swapchain images receive every displayed pixel anew.

The smaller history is a storage saving, not proof of reduced bandwidth or GPU
time. Tight HUD work is independent of TAA; compact history requires Lite and
Lite transfers. The HUD allocation itself is unchanged. No shadow quality,
resolution, visible FOV, texture filtering or effect limits are reduced.

## Validation

The host Vulkan fixture compares 3,072 frames at two odd extents with bundle and
Lite transfers Off/On, all three temporal qualities, RGBA/BGRA, direct/copied
depth, two eyes, resets and known reprojection. Alpha, peripheral pixels and raw
reset output stay exact; compact-history RGB allows one UNORM8 quantization step.
Stage/access checks cover actual barriers and conservative interop boundaries.
HUD tests cover 64 actual GPU copies/blits and exhaustive rectangle/guard plans,
including changed content, crop expansion and two in-flight swapchain images.
These fixtures are not full Vulkan synchronization validation or Quest evidence.

Build both native profiles from one clean source. Then check HUD expansion,
tooltips, promotion/radial menus, keyboard, pause, campaign transitions, pointer,
water, shadows and stereo on the headset. Compare app GPU time with the same
scene/settings, since rendering remains capped near 60 FPS. Keep source-bound
APK/signature/privacy checks and completed replay/PC-match gates before release.

References: [Vulkan dependency scopes](https://docs.vulkan.org/samples/latest/samples/performance/pipeline_barriers/README.html)
and the retained [RDR temporal filter attribution](TAA_LITE_TRIAL.md).
